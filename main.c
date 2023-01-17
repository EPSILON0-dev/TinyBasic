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
const char *rem_command = "REM";
const char *list_command = "LIST";
const char *print_command = "PRINT";
const char *goto_command = "GOTO";
const char *run_command = "RUN";
const char *end_command = "END";
const char *clear_command = "CLEAR";
const char *memory_command = "MEMORY";
// DEBUG
const char *expr_command = "EXPR";

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
  ET_SUBEXPR_OPEN,
  ET_SUBEXPR_CLOSE,
  ET_SUBEXPR = 4
};

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
size_t skip_spaces(size_t ind);
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
var_t solve_expr(size_t index, bool *error);
void expr_tokenize(size_t index);
bool expr_calc_precedence(void);
void expr_filter_brackets(void);
bool expr_reduce(void);
bool expr_reduce_unary(void);
bool expr_reduce_check(size_t index);
void expr_erase(size_t index, size_t length);

// Command execution utilities
bool command_compare(const char * restrict command, size_t index);
line_t execute_command(size_t index);
void list_handle(void);
void print_handle(size_t index);
line_t goto_handle(size_t index);
void run_handle(void);

// Main functions
bool handle_shell(void);
void execute_newline(void);

/****************************************************************************/

/**
 * Skip the spaces and tabs in the new line and return the index of first non-space
 */
size_t skip_spaces(size_t ind)
{
  while (isblank(codemem[ind])) {
    if (ind >= codemem_end)
      break;
    ind++;
  }
  return ind;
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
  while (isblank(codemem[ind]))
    ind++;

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
 * DEBUG: Try to solve the expression
 */
var_t solve_expr(size_t index, bool *error)
{
  // Skip the command
  index += strlen(expr_command);

  // Do the expression things
  expr_token_count = 0;
  expr_tokenize(index);
  if (expr_token_count == EXPR_MAX_TOKENS)
    goto handle_expr_error;

  if (expr_reduce_unary())
    goto handle_expr_error;

  if (expr_calc_precedence())
    goto handle_expr_error;

  expr_filter_brackets();

  #if EXPR_DEBUG == 1
    for (int i = 0; i < expr_token_count; i++)
      printf("Type %d, Precedence %d, Value %d\n", expr_tokens[i].type,
        expr_tokens[i].precedence, expr_tokens[i].value);
  #endif

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
void expr_tokenize(size_t index)
{
  while (1)
  {
    // Check for the end
    if (codemem[index] == '\0' || expr_token_count == EXPR_MAX_TOKENS)
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

      case ET_ADD:
      case ET_SUBTRACT:
        expr_tokens[i].precedence = base_precedence + 1;
        break;

      case ET_MULTIPLY:
      case ET_DIVIDE:
      case ET_REMAINDER:
        expr_tokens[i].precedence = base_precedence + 2;
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
  // Get the line number
  const line_t linenum = (index == codemem_end) ? 0 : *(line_t*)(&codemem[index - 2]);

  // Skip the spaces
  while (isblank(codemem[index]))
    index++;

  // Execute "PRINT"
  if (command_compare(print_command, index)) {
    print_handle(index);
  }

  // Execute "EXPR"
  else if (command_compare(expr_command, index)) {
    bool error;
    printf("%d\n", solve_expr(index, &error));
    if (error)
      return MAX_LINENUM;
  }

  // Execute "GOTO"
  else if (command_compare(goto_command, index)) {
    return goto_handle(index);
  }

  // Execute "REM" (reminder/comment command)
  else if (command_compare(rem_command, index)) {
    (void)0;
  }

  // Execute "CLEAR"
  else if (command_compare(clear_command, index)) {
    printf("\033[2J\033[H");
  }

  // Execute "END"
  else if (command_compare(end_command, index)) {
    return MAX_LINENUM;
  }

  // Execute "RUN"
  else if (command_compare(run_command, index) && !linenum) {
    run_handle();
  }

  // Execute "LIST"
  else if (command_compare(list_command, index) && !linenum) {
    list_handle();
  }

  // Execute "MEMORY"
  else if (command_compare(memory_command, index) && !linenum) {
    printf("%d bytes free\n", (int)(CODE_MEMORY_SIZE - codemem_end));
  }

  // If command wasn't recognized show an error and return
  else {
    printf("Unknown command at line %d:\n%s\n", linenum, &codemem[index]);
    return MAX_LINENUM;
  }

  return 0;
}

/**
 * Print the string, or strings if separated by ':'
 */
void print_handle(size_t index)
{
  const size_t initial_index = index;
  index += strlen(print_command);

  while (1) {

    // Skip spaces
    while (isblank(codemem[index]))
      index++;

    // Check for the string
    if (codemem[index] != '"')
      goto print_handle_err;

    // Get the string length
    size_t len;
    for (len = 0; codemem[index + len + 1] != '"'; len++)
      if (codemem[index + len + 1] == '\0')
        goto print_handle_err;

    // Print the string
    for (size_t i = 1; i <= len; i++)
      putchar(codemem[index + i]);

    // Move the index
    index += len + 2;
    while (isblank(codemem[index]))
      index++;

    // Continue while there are more strings
    if (codemem[index] == ':') {
      index++;
    } else {
      break;
    }
  }

  // Show an error if there's something after the string
  if (codemem[index] != '\0')
    goto print_handle_err;

  // Print the LF and return
  printf("\n\r");
  return;

  // Syntax error print
  print_handle_err: (void)0;
  const line_t linenum = (initial_index == codemem_end) ? 0 :
    *(line_t*)(&codemem[initial_index - 2]);
  printf("\nSyntax error at line %d:\n%s\n", linenum, &codemem[initial_index]);
}

/**
 * List lines, their numbers and indices
 */
void list_handle(void)
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
line_t goto_handle(size_t index)
{
  const size_t initial_index = index;
  index += strlen(goto_command);
  while (isblank(codemem[index]))
    index++;
  const line_t linenum = get_literal_number(index);
  if (linenum <= 0 || linenum >= MAX_LINENUM)
    goto goto_handle_err;
  return linenum;

  // Syntax error print
  goto_handle_err: (void)0;
  const line_t errlinenum = (initial_index == codemem_end) ? 0 :
    *(line_t*)(&codemem[initial_index - 2]);
  printf("\nSyntax error at line %d:\n%s\n", errlinenum, &codemem[initial_index]);
  return MAX_LINENUM;
}

/**
 * Start the program execution
 */
void run_handle(void)
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
  const size_t ind = skip_spaces(newline_ind);
  if (ind == newline_end)
    return;

  // Check if the command starts with linenumber
  if (isdigit(codemem[ind])) {
    store_newline(ind);
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
