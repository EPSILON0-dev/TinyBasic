#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#define CODE_MEMORY_SIZE 8192
#define EXPR_MAX_TOKENS 64
#define MAX_LINENUM 10000
#define LIST_DEBUG 0
#define EXPR_DEBUG 0

typedef uint16_t line_t;
typedef int32_t var_t;

/****************************************************************************/

// Command constants
const char *kwd_let    = "LET";
const char *kwd_list   = "LIST";
const char *kwd_print  = "PRINT";
const char *kwd_goto   = "GOTO";
const char *kwd_if     = "IF";
const char *kwd_then   = "THEN";
const char *kwd_rem    = "REM";
const char *kwd_run    = "RUN";
const char *kwd_clear  = "CLEAR";
const char *kwd_memory = "MEMORY";
const char *kwd_end    = "END";

/****************************************************************************/

// Expressions stuff
enum EExprTokens {
  ET_NONE,
  ET_VALUE,
  ET_ADD,
  ET_SUBTRACT,
  ET_MULTIPLY,
  ET_DIVIDE,
  ET_REMAINDER,
  ET_AND,
  ET_OR,
  ET_XOR,
  ET_SUBEXPR_OPEN,
  ET_SUBEXPR_CLOSE,
  ET_SUBEXPR = 4
};

// Compare operations
enum ECompareOperations {
  CO_EQUAL,
  CO_NOT_EQUAL,
  CO_GREATER,
  CO_LOWER
};

// Expression tokens
typedef struct ExprToken ExprToken;
struct ExprToken {
  uint8_t type;
  uint8_t precedence;
  var_t value;
};

/****************************************************************************/

// Token space for the expression solver
static ExprToken expr_tokens[EXPR_MAX_TOKENS];
static size_t expr_token_count;

// Variables for each letter of the alphabet
static var_t variables[26];

// Code memory
static char codemem[CODE_MEMORY_SIZE];
static size_t codemem_end; // The byte after the last saved line
static size_t newline_ind; // The first byte of the new line
static size_t newline_end; // The byte after the new line

/****************************************************************************/

// Command handling utilities
static inline void skip_spaces(size_t *index);
var_t get_literal_number(size_t ind);

// Code memory handling
line_t get_line_num(size_t ind);
size_t get_line_index(line_t linenum);
size_t get_line_next_index(line_t linenum);
void codemem_shift_left(size_t index, size_t length, size_t amount);
void codemem_shift_right(size_t index, size_t length, size_t amount);
void insert_line(size_t ind);
void store_newline(size_t ind);

// Expression solving
var_t expr_solve(size_t index, size_t length, bool *error);
void expr_tokenize(size_t index, size_t length);
bool expr_calc_precedence(void);
void expr_filter_brackets(void);
bool expr_reduce(void);
bool expr_reduce_unary(void);
bool expr_reduce_check(size_t index);
void expr_erase(size_t index, size_t length);

// Command execution utilities
bool command_compare(const char * restrict command, size_t index);
line_t execute_command(size_t index);
void handle_let(size_t index, bool *error);
void handle_print(size_t index, bool *error);
size_t handle_if(size_t index);
line_t handle_goto(size_t index);
void handle_list(void);
void handle_run(void);

// Main functions
bool handle_shell(void);
void execute_newline(void);

/****************************************************************************/

/**
 * Skip the spaces and tabs in the new line and return the index of first non-space
 */
static inline void skip_spaces(size_t *index)
{
  while (isblank(codemem[*index]))
    (*index)++;
}

/**
 * Get the number from the literal pointed to by index
 */
var_t get_literal_number(size_t ind)
{
  bool inverted = false;
  if (codemem[ind] == '-') {
    ind++;
    inverted = true;
  }

  var_t result = 0;
  while (isdigit(codemem[ind])) {
    result *= 10;
    result += codemem[ind++] - '0';
  }

  return (inverted) ? -result : result;
}

/****************************************************************************/

/**
 * Get the line number from the codemem at the given index
 */
line_t get_line_num(size_t ind)
{
  const var_t linenum_raw = get_literal_number(ind);
  if (linenum_raw <= 0 || linenum_raw >= MAX_LINENUM) {
    return 0;
  }
  return linenum_raw;
}

/**
 * Get the index of the line start (after line number) from codemem
 */
size_t get_line_index(line_t linenum)
{
  size_t index = 0;
  while (index < codemem_end) {
    if (*(line_t*)(&codemem[index]) == linenum)
      break;

    index += sizeof(line_t);
    while (codemem[index++] != '\0') {
      if (index >= codemem_end)
        break;
    }
  }

  return index + sizeof(line_t);
}

/**
 * Get the potential index of the line to be placed in a codemem
 */
size_t get_potential_line_index(line_t linenum)
{
  size_t index = 0;
  while (index < codemem_end) {
    if (*(line_t*)(&codemem[index]) >= linenum)
      break;

    index += sizeof(line_t);
    while (codemem[index++] != '\0') {
      if (index >= codemem_end)
        break;
    }
  }

  return index + sizeof(line_t);
}

/**
 * Shift the memory contents amount bytes left
 */
void codemem_shift_left(size_t index, size_t length, size_t amount)
{
  for (size_t i = 0; i < length; i++)
    codemem[index - amount + i] = codemem[index + i];
}

/**
 * Shift the memory contents amount bytes right
 */
void codemem_shift_right(size_t index, size_t length, size_t amount)
{
  while (length--)
    codemem[index + amount + length] = codemem[index + length];
}

/**
 * Store the new line to the memory
 */
void store_newline(size_t ind)
{
  // Get the line num
  const line_t linenum = get_line_num(ind);
  if (linenum == 0) {
    printf("Invalid line number");
    newline_end = newline_ind;
    return;
  }

  // Get the index after the number
  while (isdigit(codemem[ind]))
    ind++;
  skip_spaces(&ind);

  // Get line indices
  size_t lineind = get_line_index(linenum);

  // Delete the line if it exists
  if (lineind < codemem_end) {
    lineind -= sizeof(line_t);
    const size_t linelen = strlen(&codemem[lineind + sizeof(line_t)]) + sizeof(line_t) + 1;
    const size_t shift_length = codemem_end - (lineind + linelen);
    codemem_shift_left(lineind + linelen, shift_length, linelen);
    codemem_end -= linelen;
  } else {
    lineind = get_potential_line_index(linenum) - sizeof(line_t);
  }

  // Check if the line is non-empty
  size_t newlinelen = newline_end - ind;
  if (newlinelen) {

    // Clear the whitespaces after the command
    while (isblank(codemem[ind + newlinelen - 1]))
      newlinelen--;

    // Copy the line to the end of the memory
    for (int i = 0; i < newlinelen; i++)
      codemem[CODE_MEMORY_SIZE - newlinelen + i] = codemem[ind + i];

    // Make space for a new line
    size_t shift_amount = newlinelen + sizeof(line_t) + 1;
    if (lineind < codemem_end) {
      size_t shift_length = codemem_end - lineind;
      codemem_shift_right(lineind, shift_length, shift_amount);
    }
    codemem_end += shift_amount;

    // Copy the line into it's new place
    *(line_t*)(&codemem[lineind]) = linenum;
    codemem[lineind + newlinelen + sizeof(line_t)] = '\0';
    for (int i = 0; i < newlinelen; i++)
      codemem[lineind + sizeof(line_t) + i] = codemem[CODE_MEMORY_SIZE - newlinelen + i];
  }
}

/****************************************************************************/

/**
 * Solve the expression and return the result
 */
var_t expr_solve(size_t index, size_t length, bool *error)
{
  // Do the expression things
  expr_token_count = 0;
  expr_tokenize(index, length);
  if (expr_token_count == EXPR_MAX_TOKENS)
    goto handle_expr_error;

  #if EXPR_DEBUG == 1
  for (int i = 0; i < expr_token_count; i++)
    printf("Type %d, Precedence %d, Value %d\n", expr_tokens[i].type,
      expr_tokens[i].precedence, expr_tokens[i].value);
  #endif

  if (expr_reduce_unary())
    goto handle_expr_error;

  if (expr_calc_precedence())
    goto handle_expr_error;

  expr_filter_brackets();

  // Solve the expression
  while (expr_token_count > 1)
    if (expr_reduce())
      goto handle_expr_error;

  // DEBUG
  *error = 0;
  return expr_tokens[0].value;

  // Syntax error
  handle_expr_error:
  printf("Syntax error in expression: %s\n", &codemem[index]);
  *error = 1;
  return 0;
}

/**
 * Tokenize the expression
 */
void expr_tokenize(size_t index, size_t length)
{
  length += index;
  while (1)
  {
    // Check for the end
    if (expr_token_count == EXPR_MAX_TOKENS)
      break;
    if (index >= length)
      break;

    ExprToken tok;
    tok.type = ET_NONE;
    tok.precedence = 0;
    tok.value = 0;

    // Check for the literals
    if (isdigit(codemem[index])) {
      tok.type = ET_VALUE;
      tok.value = get_literal_number(index);
      while (isdigit(codemem[index]))
        index++;
      index--;
    }

    // Check for the variables
    else if (isalpha(codemem[index])) {
      tok.type = ET_VALUE;
      tok.value = variables[toupper(codemem[index]) - 'A'];
    }

    // Check the remaining tokens
    else switch (codemem[index]) {
      case ' ':
      case '\t':
        break;

      case '+':
        tok.type = ET_ADD;
        break;

      case '-':
        tok.type = ET_SUBTRACT;
        break;

      case '*':
        tok.type = ET_MULTIPLY;
        break;

      case '/':
        tok.type = ET_DIVIDE;
        break;

      case '%':
        tok.type = ET_REMAINDER;
        break;

      case '&':
        tok.type = ET_AND;
        break;

      case '|':
        tok.type = ET_OR;
        break;

      case '^':
        tok.type = ET_XOR;
        break;

      case '(':
        tok.type = ET_SUBEXPR_OPEN;
        break;

      case ')':
        tok.type = ET_SUBEXPR_CLOSE;
        break;

      default:
        expr_token_count = EXPR_MAX_TOKENS;
    }

    if (tok.type != ET_NONE)
      expr_tokens[expr_token_count++] = tok;

    index++;
  }
}

/**
 * Calculate expression precedence
 */
bool expr_calc_precedence(void)
{
  int8_t base_precedence = 0;
  for (int i = 0; i < expr_token_count; i++) {
    switch (expr_tokens[i].type) {

      case ET_AND:
      case ET_OR:
      case ET_XOR:
        expr_tokens[i].precedence = base_precedence + 2;
        break;

      case ET_ADD:
      case ET_SUBTRACT:
        expr_tokens[i].precedence = base_precedence + 2;
        break;

      case ET_MULTIPLY:
      case ET_DIVIDE:
      case ET_REMAINDER:
        expr_tokens[i].precedence = base_precedence + 3;
        break;

      case ET_SUBEXPR_OPEN:
        base_precedence += ET_SUBEXPR;
        break;

      case ET_SUBEXPR_CLOSE:
        base_precedence -= ET_SUBEXPR;
        break;
    }

    if (base_precedence < 0)
      return 1;
  }

  return !!base_precedence;
}

/**
 * Filter the brackets from the expression
 */
void expr_filter_brackets(void)
{
  size_t index = 0, newindex = 0;
  for (; index < expr_token_count; index++) {
    const uint8_t type = expr_tokens[index].type;
    if (type != ET_SUBEXPR_OPEN && type != ET_SUBEXPR_CLOSE)
      expr_tokens[newindex++] = expr_tokens[index];
  }
  expr_token_count = newindex;
}

/**
 * Try to solve highest precedence operation
 */
bool expr_reduce(void)
{
  // Find most important operation index
  uint8_t prec = 0;
  size_t index = 0;
  for (size_t i = 0; i < expr_token_count; i++) {
    if (expr_tokens[i].precedence > prec) {
      prec = expr_tokens[i].precedence;
      index = i;
    }
  }

  // Return 1 if no operation can be performed
  if (!prec)
    return 1;

  // Do the multiplication
  if (expr_tokens[index].type == ET_MULTIPLY) {
    if (expr_reduce_check(index))
      return 1;
    expr_tokens[index - 1].value *= expr_tokens[index + 1].value;
    expr_erase(index, 2);
    return 0;
  }

  // Do the division
  else if (expr_tokens[index].type == ET_DIVIDE) {
    if (expr_reduce_check(index))
      return 1;
    expr_tokens[index - 1].value /= expr_tokens[index + 1].value;
    expr_erase(index, 2);
    return 0;
  }

  // Do the remainder
  else if (expr_tokens[index].type == ET_REMAINDER) {
    if (expr_reduce_check(index))
      return 1;
    expr_tokens[index - 1].value %= expr_tokens[index + 1].value;
    expr_erase(index, 2);
    return 0;
  }

  // Do the addition
  else if (expr_tokens[index].type == ET_ADD) {
    if (expr_reduce_check(index))
      return 1;
    expr_tokens[index - 1].value += expr_tokens[index + 1].value;
    expr_erase(index, 2);
    return 0;
  }

  // Do the subtraction
  else if (expr_tokens[index].type == ET_SUBTRACT) {
    if (expr_reduce_check(index))
      return 1;
    expr_tokens[index - 1].value -= expr_tokens[index + 1].value;
    expr_erase(index, 2);
    return 0;
  }

  // Do the AND
  else if (expr_tokens[index].type == ET_AND) {
    if (expr_reduce_check(index))
      return 1;
    expr_tokens[index - 1].value &= expr_tokens[index + 1].value;
    expr_erase(index, 2);
    return 0;
  }

 // Do the OR
  else if (expr_tokens[index].type == ET_OR) {
    if (expr_reduce_check(index))
      return 1;
    expr_tokens[index - 1].value |= expr_tokens[index + 1].value;
    expr_erase(index, 2);
    return 0;
  }

 // Do the XOR
  else if (expr_tokens[index].type == ET_XOR) {
    if (expr_reduce_check(index))
      return 1;
    expr_tokens[index - 1].value ^= expr_tokens[index + 1].value;
    expr_erase(index, 2);
    return 0;
  }

  return 1;
}

/**
 * Reduce the unary operators
 */
bool expr_reduce_unary(void)
{
  for (size_t i = expr_token_count; i > 0; i--)
  {
    if (expr_tokens[i].type == ET_SUBEXPR_OPEN)
      continue;
    if (i > 1 && expr_tokens[i - 2].type == ET_VALUE)
      continue;
    if (i > 1 && expr_tokens[i - 2].type == ET_SUBEXPR_CLOSE)
      continue;

    if (expr_tokens[i - 1].type == ET_ADD) {
      if (expr_tokens[i].type != ET_VALUE)
        return 1;
      expr_erase(i - 1, 1);
    } else if (expr_tokens[i - 1].type == ET_SUBTRACT) {
      if (expr_tokens[i].type != ET_VALUE)
        return 1;
      expr_tokens[i].value *= -1;
      expr_erase(i - 1, 1);
    }
  }

  return 0;
}

/**
 * Check if the operation is valid
 */
bool expr_reduce_check(size_t index)
{
  if (index == 0 || index == expr_token_count)
    return 1;

  if (expr_tokens[index - 1].type != ET_VALUE)
    return 1;

  if (expr_tokens[index + 1].type != ET_VALUE)
    return 1;

  return 0;
}

/**
 * Erase length tokens at index
 */
void expr_erase(size_t index, size_t length)
{
  for (int i = index; i < expr_token_count - length; i++)
    expr_tokens[i] = expr_tokens[i + length];
  expr_token_count -= length;
}

/****************************************************************************/

/**
 * Compare the memory contents to the given command
 */
bool command_compare(const char * restrict command, size_t index)
{
  size_t i;
  for (i = 0; command[i]; i++)
    if (command[i] != toupper(codemem[index + i]))
      return false;
  return (codemem[index + i] == '\0' || codemem[index + i] == ' ');
}

/**
 * Detect the command and call the appropriate method on it and return next line address
 */
line_t execute_command(size_t index)
{
  bool error;

  // Execute "LET"
  if (command_compare(kwd_let, index)) {
    handle_let(index + strlen(kwd_list), &error);
  }

  // Execute "PRINT"
  else if (command_compare(kwd_print, index)) {
    handle_print(index, &error);
  }

  // Execute "GOTO"
  else if (command_compare(kwd_goto, index)) {
    return handle_goto(index);
  }

  // Execute "IF"
  else if (command_compare(kwd_if, index)) {
    return handle_if(index);
  }

  // Execute "REM" (reminder/comment command)
  else if (command_compare(kwd_rem, index)) {
    (void)0;
  }

  // Execute "CLEAR"
  else if (command_compare(kwd_clear, index)) {
    printf("\033[2J\033[H");
  }

  // Execute "END"
  else if (command_compare(kwd_end, index)) {
    return MAX_LINENUM;
  }

  // Execute "RUN"
  else if (command_compare(kwd_run, index)) {
    handle_run();
  }

  // Execute "LIST"
  else if (command_compare(kwd_list, index)) {
    handle_list();
  }

  // Execute "MEMORY"
  else if (command_compare(kwd_memory, index)) {
    printf("%d bytes free\n", (int)(CODE_MEMORY_SIZE - codemem_end));
  }

  // Execute "LET" without the keyword
  else if (isalpha(codemem[index]) && (codemem[index + 1] == ' ' || codemem[index + 1] == '=')) {
    handle_let(index, &error);
  }

  // If command wasn't recognized show an error and return
  else {
    printf("Unknown command:\n%s\n", &codemem[index]);
    return MAX_LINENUM;
  }

  return (error) ? MAX_LINENUM : 0;
}

/**
 * Print the string, or strings if separated by ':'
 */
void handle_print(size_t index, bool *error)
{
  const size_t initial_index = index;
  index += strlen(kwd_print);
  bool linefeed = true;

  while (1) {

    if (codemem[index] == '\0') {
      linefeed = false;
      break;
    }

    skip_spaces(&index);

    // Handle string or the expression
    if (codemem[index] == '"') {

      // Get the string length
      size_t len;
      for (len = 0; codemem[index + len + 1] != '"'; len++)
        if (codemem[index + len + 1] == '\0')
          goto handle_print_err;

      // Print the string
      for (size_t i = 1; i <= len; i++)
        putchar(codemem[index + i]);

      // Move the index
      index += len + 2;
      skip_spaces(&index);

      // Continue while there are more expressions or strings
      if (codemem[index] == ':') {
        index++;
      } else {
        break;
      }
    } else {

      // Get the expression length
      size_t length = 0;
      while (codemem[index + length] != '\0' && codemem[index + length] != ':')
        length++;

      // Get the expression value and print it out
      const var_t expr_value = expr_solve(index, length, error);
      if (*error)
        return;

      // Print the expression result
      // NOLINTNEXTLINE
      printf("%ld", expr_value);

      // Continue while there are more expressions or strings
      index += length;
      if (codemem[index] == ':') {
        index++;
      } else {
        break;
      }
    }
  }

  // Show an error if there's something after the string
  if (codemem[index] != '\0')
    goto handle_print_err;

  // Print the LF and return
  if (linefeed)
    printf("\n");
  *error = false;
  return;

  // Syntax error print
  handle_print_err: (void)0;
  printf("\nSyntax error:\n%s\n", &codemem[initial_index]);
  *error = true;
  return;
}

/**
 * Handle let command, solve the expression and do the assignment
 */
void handle_let(size_t index, bool *error)
{
  const size_t initial_index = index;

  // Get the target variable
  skip_spaces(&index);
  if (!isalpha(codemem[index]))
    goto handle_let_err;
  const size_t variable = toupper(codemem[index]) - 'A';

  // Check for the equal symbol sanity
  index++;
  skip_spaces(&index);
  if (codemem[index] != '=')
    goto handle_let_err;

  // Get the expression length
  index++;
  size_t length = 0;
  while (codemem[index + length] != '\0')
    length++;

  // Solve the expression and assign the value
  const var_t expr_value = expr_solve(index, length, error);
  if (*error)
    return;
  variables[variable] = expr_value;
  *error = false;
  return;

  // Syntax error print
  handle_let_err: (void)0;
  printf("\nSyntax error:\n%s\n", &codemem[initial_index]);
  *error = true;
  return;
}

/**
 * List lines, their numbers and indices
 */
void handle_list(void)
{
  size_t index = 0;
  while (index < codemem_end) {
    const line_t linenum = *(line_t*)(&codemem[index]);
    const size_t linelen = strlen(&codemem[index + sizeof(line_t)]);
    #if LIST_DEBUG == 1
      printf("index: %zu, linelen: %zu, linenum: %d # %s\n",
        index, linelen, linenum, &codemem[index + sizeof(line_t)]);
    #else
      printf("%d %s\n", linenum, &codemem[index + sizeof(line_t)]);
    #endif
    index += linelen + sizeof(line_t) + 1;
  }
}

/**
 * Get the GOTO target line
 */
line_t handle_goto(size_t index)
{
  const size_t initial_index = index;
  index += strlen(kwd_goto);
  skip_spaces(&index);
  const line_t linenum = get_literal_number(index);
  if (linenum <= 0 || linenum >= MAX_LINENUM)
    goto handle_goto_err;
  return linenum;

  // Syntax error print
  handle_goto_err: (void)0;
  printf("\nSyntax error:\n%s\n", &codemem[initial_index]);
  return MAX_LINENUM;
}

/**
 * Check the condition and execute the command if it's met
 */
size_t handle_if(size_t index)
{
  const size_t initial_index = index;
  bool error;
  index += strlen(kwd_if);
  skip_spaces(&index);

  // Do the first expression
  size_t length = 0;
  while (1) {
    const char chr = codemem[index + length];
    if (chr == '<' || chr == '>' || chr == '=' || chr == '\0')
      break;
    length++;
  }
  if (codemem[index + length] == '\0')
    goto handle_if_err;
  var_t expr_left_value = expr_solve(index, length, &error);
  if (error)
    return MAX_LINENUM;

  // Check what operation needs to be done
  uint8_t compare_operation;
  index += length;
  if (codemem[index] == '<') {
    if (codemem[index + 1] == '>') {
      compare_operation = CO_NOT_EQUAL;
      index += 2;
    } else {
      compare_operation = CO_LOWER;
      index++;
    }
  } else if (codemem[index] == '>') {
    compare_operation = CO_GREATER;
    index++;
  } else if (codemem[index] == '=') {
    compare_operation = CO_EQUAL;
    index++;
  } else {
    goto handle_if_err;
  }

  // Get the second expression
  length = 0;
  while (1) {
    if (codemem[index + length] == '\0')
      goto handle_if_err;
    if (command_compare(kwd_then, index + length))
      break;
    length++;
  }
  var_t expr_right_value = expr_solve(index, length, &error);
  if (error)
    return MAX_LINENUM;

  // Check the condition
  bool condition;
  switch (compare_operation) {
    case CO_EQUAL:
      condition = (expr_left_value == expr_right_value);
      break;
    case CO_NOT_EQUAL:
      condition = (expr_left_value != expr_right_value);
      break;
    case CO_LOWER:
      condition = (expr_left_value  < expr_right_value);
      break;
    case CO_GREATER:
      condition = (expr_left_value  > expr_right_value);
      break;
  }

  // Do the next line if condition met
  if (condition) {
    index += length + strlen(kwd_then);
    skip_spaces(&index);
    return execute_command(index);
  } else {
    return 0;
  }

  // Syntax error print
  handle_if_err: (void)0;
  const line_t linenum = (initial_index == codemem_end) ? 0 :
    *(line_t*)(&codemem[initial_index - 2]);
  printf("\nSyntax error at line %d:\n%s\n", linenum, &codemem[initial_index]);
  return MAX_LINENUM;
}

/**
 * Start the program execution
 */
void handle_run(void)
{
  // Skip if no code exists
  if (!codemem_end) {
    printf("No code to run, go write some\n");
    return;
  }

  size_t index = sizeof(line_t);
  while (1) {

    // Execute a line
    line_t nextline = execute_command(index);

    // Exit if error occured
    if (nextline == MAX_LINENUM) {
      return;
    }

    // Find the next line index if not given
    else if (!nextline) {
      index += strlen(&codemem[index]) + sizeof(line_t) + 1;
      if (index >= codemem_end)
        return;
    }

    // Find the line index based on the line number
    else {
      index = get_line_index(nextline);
      if (index == codemem_end) {
        printf("Line %d not found.\n", nextline);
        return;
      }
    }
  }
}

/****************************************************************************/

/**
 * Execute the "newline command" (or store in codemem if needed)
 */
void execute_newline(void)
{
  // Skip starting spaces and tabs
  size_t index = newline_ind;
  skip_spaces(&index);
  if (index == newline_end)
    return;

  // Check if the command starts with linenumber
  if (isdigit(codemem[index])) {
    store_newline(index);
  } else {
    codemem[newline_end] = '\0';
    execute_command(newline_ind);
  }

  // "Clear" the newline buffer
  newline_ind = codemem_end;
  newline_end = codemem_end;
}

/**
 * Handle the shell input, return true if command has to be ran
 */
bool handle_shell(void)
{
  const char chr = getchar();

  // If it was backspace delete the character from the line
  if (chr == '\b') {
    if (newline_end > newline_ind)
      newline_end--;
    return false;
  }

  // If it was line feed execute the line
  else if (chr == '\n') {
    return true;
  }

  // If it wasn't line feed add the character to the memory
  else if (newline_end < CODE_MEMORY_SIZE) {
    codemem[newline_end++] = chr;
    return false;
  }

  return false;
}

/****************************************************************************/

int main(void)
{
  // Preset the static variables
  codemem_end = 0;
  newline_ind = 0;
  newline_end = 0;
  expr_token_count = 0;

  for (int i = 0; i < 26; i++)
    variables[i] = 0;

  // Show the prompt
  printf("\nTinyBasic by EPSILON0\nExpression limit: %d tokens\nCode memory: %d bytes\n\n> ",
    EXPR_MAX_TOKENS, CODE_MEMORY_SIZE);

  // Main loop
  while (1) {
    const bool execute = handle_shell();

    if (execute) {
      execute_newline();
      printf("> ");
    }

  }

  return 0;
}
