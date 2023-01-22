/**
 * Copyright Lukasz Forenc 2023
 *
 * File: main.c
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#define CODE_MEMORY_SIZE  8192
#define EXPR_MAX_TOKENS   64
#define MAX_LINENUM       10000
#define POKE_PEEK         0
#define FILE_IO           1
#define IO_KILL           1

typedef uint16_t line_t;
typedef int32_t var_t;
typedef size_t peek_t;

#define IO_INIT() ((void)0)
#define PUTCHAR(x) (putchar(x))
#define GETCHAR() (getchar())

/****************************************************************************/

#define LIST_DEBUG        0
#define EXPR_DEBUG        0

/****************************************************************************/

// Command constants
const char *kwd_clear  = "CLEAR";
const char *kwd_end    = "END";
const char *kwd_goto   = "GOTO";
const char *kwd_if     = "IF";
const char *kwd_input  = "INPUT";
const char *kwd_let    = "LET";
const char *kwd_list   = "LIST";
const char *kwd_memory = "MEMORY";
const char *kwd_new    = "NEW";
const char *kwd_print  = "PRINT";
const char *kwd_rem    = "REM";
const char *kwd_run    = "RUN";
const char *kwd_then   = "THEN";
#if POKE_PEEK == 1
const char *kwd_peek   = "PEEK";
const char *kwd_poke   = "POKE";
#endif
#if FILE_IO == 1
const char *kwd_load   = "LOAD";
const char *kwd_save   = "SAVE";
#endif

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
  ET_INVERT,
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

// Current line for when the code is executing
static line_t current_line;

/****************************************************************************/

// Printing utilities
void print_string(const char *string);
void print_unsigned(uint64_t value);
void print_signed(int64_t value);

// Command handling utilities
static inline void skip_spaces(size_t *index);
var_t get_literal_number(size_t index, bool *error);

// Code memory handling
line_t get_line_num(size_t index);
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
line_t print_error(const char * restrict error, size_t index);
line_t execute_command(size_t index);
line_t handle_let(size_t index);
line_t handle_print(size_t index);
line_t handle_if(size_t index);
line_t handle_goto(size_t index);
line_t handle_input(size_t index);
void handle_list(void);
void handle_new(void);
void handle_run(void);
#if POKE_PEEK == 1
line_t handle_poke(size_t index);
line_t handle_peek(size_t index);
#endif
#if FILE_IO == 1
void handle_save(size_t index);
void handle_load(size_t index);
#endif

// Main functions
bool handle_shell(void);
void execute_newline(void);

/****************************************************************************/

/**
 * Print out a string
 */
void print_string(const char *string)
{
  while (*string)
    PUTCHAR(*(string++));
}

/**
 * Print out an unsigned number
 */
void print_unsigned(uint64_t value)
{
  size_t length = 1;
  uint64_t tvalue = value;
  while (tvalue > 9) {
    length++;
    tvalue /= 10;
  }

  while (length--) {
    tvalue = value;
    for (size_t i = 0; i < length; i++)
      tvalue /= 10;
    tvalue %= 10;
    PUTCHAR(tvalue + '0');
  }
}

/**
 * Print out a signed number
 */
void print_signed(int64_t value)
{
  if (value < 0) {
    PUTCHAR('-');
    value = -value;
  }
  print_unsigned(value);
}

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
var_t get_literal_number(size_t index, bool *error)
{
  var_t result = 0;
  size_t length = 0;
  while (isalnum(codemem[index + length]) && index + length < newline_end)
    length++;

  // Do the binary
  if (length > 2 && codemem[index] == '0' && codemem[index + 1] == 'b') {
    index += 2;
    while (isdigit(codemem[index])) {
      char digit = codemem[index++] - '0';
      if (digit > 1) {
        *error = true;
        return 0;
      }
      result *= 2;
      result += digit;
    }
  }

  // Do the hex
  else if (length > 2 && codemem[index] == '0' && codemem[index + 1] == 'x') {
    index += 2;
    while (isxdigit(codemem[index])) {
      char digit = toupper(codemem[index++]);
      digit -= (digit > '9') ? 'A' - 10 : '0';
      if (digit > 16) {
        *error = true;
        return 0;
      }
      result *= 16;
      result += digit;
    }
  }

  // Do the octal
  else if (length > 1 && codemem[index] == '0') {
    index++;
    while (isdigit(codemem[index])) {
      char digit = codemem[index++] - '0';
      if (digit > 7) {
        *error = true;
        return 0;
      }
      result *= 8;
      result += digit;
    }
  }

  // Do the decimal
  else {
    while (isdigit(codemem[index])) {
      result *= 10;
      result += codemem[index++] - '0';
    }
  }

  *error = false;
  return result;
}

/****************************************************************************/

/**
 * Get the line number from the codemem at the given index
 */
line_t get_line_num(size_t index)
{
  bool error;
  const var_t linenum_raw = get_literal_number(index, &error);
  if (linenum_raw <= 0 || linenum_raw >= MAX_LINENUM || error)
    return 0;
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
    print_string("Invalid line number");
    newline_end = newline_ind;
    return;
  }

  // Get the index after the number
  while (isalnum(codemem[ind]) && ind < newline_end)
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

  if (expr_reduce_unary())
    goto handle_expr_error;

  if (expr_calc_precedence())
    goto handle_expr_error;

  expr_filter_brackets();

  #if EXPR_DEBUG == 1
  for (int i = 0; i < expr_token_count; i++)
    printf("Type %d, Precedence %2d, Value 0x%lx (%ld)\n", expr_tokens[i].type,
      expr_tokens[i].precedence, expr_tokens[i].value, expr_tokens[i].value); // NOLINT
  #endif

  // Solve the expression
  while (expr_token_count > 1)
    if (expr_reduce())
      goto handle_expr_error;

  // Return the result
  *error = 0;
  return expr_tokens[0].value;

  // Syntax error
  handle_expr_error:
  print_error("Failed to evaluate expression", index);
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
      bool error;
      tok.type = ET_VALUE;
      tok.value = get_literal_number(index, &error);
      if (error) {
        expr_token_count = EXPR_MAX_TOKENS;
        break;
      }
      while (isalnum(codemem[index]))
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

      case '!':
        tok.type = ET_INVERT;
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
        expr_tokens[i].precedence = base_precedence + 1;
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
    } else if (expr_tokens[i - 1].type == ET_INVERT) {
      if (expr_tokens[i].type != ET_VALUE)
        return 1;
      expr_tokens[i].value = ~expr_tokens[i].value;
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
 * Show the error
 */
line_t print_error(const char * restrict error, size_t index)
{
  if (current_line) {
    print_string("Error at line ");
    print_unsigned(current_line);
    print_string(": ");
    print_string(error);
    print_string("\n");
    print_unsigned(current_line);
    print_string(" ");
    print_string(&codemem[index]);
    print_string("\n");
  } else {
    print_string("Error: ");
    print_string(error);
    print_string("\n");
    print_string(&codemem[index]);
    print_string("\n");
  }
  return MAX_LINENUM;
}

/**
 * Detect the command and call the appropriate method on it and return next line address
 */
line_t execute_command(size_t index)
{
  bool error = false;

  // Execute "LET"
  if (command_compare(kwd_let, index)) {
    return handle_let(index + strlen(kwd_list));
  }

  // Execute "LET" without the keyword
  else if (isalpha(codemem[index]) && (codemem[index + 1] == ' ' || codemem[index + 1] == '=')) {
    return handle_let(index);
  }

  // Execute "PRINT"
  else if (command_compare(kwd_print, index)) {
    return handle_print(index);
  }

  // Execute "GOTO"
  else if (command_compare(kwd_goto, index)) {
    return handle_goto(index);
  }

  // Execute "IF"
  else if (command_compare(kwd_if, index)) {
    return handle_if(index);
  }

  #if POKE_PEEK == 1
  // Execute "POKE"
  else if (command_compare(kwd_poke, index)) {
    return handle_poke(index);
  }

  // Execute "PEEK"
  else if (command_compare(kwd_peek, index)) {
    return handle_peek(index);
  }
  #endif

  // Execute "INPUT"
  else if (command_compare(kwd_input, index)) {
    handle_input(index);
  }

  // Execute "REM" (reminder/comment command)
  else if (command_compare(kwd_rem, index)) {
    (void)0;
  }

  // Execute "CLEAR"
  else if (command_compare(kwd_clear, index)) {
    print_string("\033[2J\033[H");
  }

  // Execute "END"
  else if (command_compare(kwd_end, index)) {
    return MAX_LINENUM;
  }

  // Execute "RUN"
  else if (command_compare(kwd_run, index)) {
    if (!current_line)
      handle_run();
    else
      print_error("Command unavailable during run mode", index);
  }

  // Execute "LIST"
  else if (command_compare(kwd_list, index)) {
    if (!current_line)
      handle_list();
    else
      print_error("Command unavailable during run mode", index);
  }

  // Execute "NEW"
  else if (command_compare(kwd_new, index)) {
    if (!current_line)
      handle_new();
    else
      print_error("Command unavailable during run mode", index);
  }

  // Execute "MEMORY"
  else if (command_compare(kwd_memory, index)) {
    if (!current_line) {
      print_signed((int)(CODE_MEMORY_SIZE - codemem_end));
      print_string(" bytes free\n");
    } else {
      print_error("Command unavailable during run mode", index);
    }
  }

  #if FILE_IO == 1
  // Execute "SAVE"
  else if (command_compare(kwd_save, index)) {
    if (!current_line) {
      handle_save(index);
    } else {
      print_error("Command unavailable during run mode", index);
    }
  }

  // Execute "LOAD"
  else if (command_compare(kwd_load, index)) {
    if (!current_line) {
      handle_load(index);
    } else {
      print_error("Command unavailable during run mode", index);
    }
  }
  #endif

  // If command wasn't recognized show an error and return
  else {
    return print_error("Unknown command", index);
  }

  return (error) ? MAX_LINENUM : 0;
}

/**
 * Print the string, or strings if separated by ':'
 */
line_t handle_print(size_t index)
{
  const size_t initial_index = index;
  index += strlen(kwd_print);
  bool linefeed = true;

  while (1) {

    // Disable linefeed if the line ended with the concat operator
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
          return print_error("Unclosed string", initial_index);

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
      bool error;
      const var_t expr_value = expr_solve(index, length, &error);
      if (error)
        return MAX_LINENUM;

      // Print the expression result
      // NOLINTNEXTLINE
      print_signed((int64_t)expr_value);

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
    return print_error("Invalid data after print statement", initial_index);

  // Print the LF and return
  if (linefeed)
    print_string("\n");
  return 0;
}

/**
 * Handle let command, solve the expression and do the assignment
 */
line_t handle_let(size_t index)
{
  const size_t initial_index = index;

  // Get the target variable
  skip_spaces(&index);
  if (!isalpha(codemem[index]))
    return print_error("Invalid target variable", initial_index);
  const size_t variable = toupper(codemem[index]) - 'A';

  // Check for the equal symbol sanity
  index++;
  skip_spaces(&index);
  if (codemem[index] != '=')
    return print_error("Expected '=' token after the target variable", initial_index);

  // Get the expression length
  index++;
  size_t length = 0;
  while (codemem[index + length] != '\0')
    length++;

  // Solve the expression and assign the value
  bool error;
  const var_t expr_value = expr_solve(index, length, &error);
  if (error)
    return MAX_LINENUM;
  variables[variable] = expr_value;
  return 0;
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
      print_unsigned(linenum);
      print_string(" ");
      print_string(&codemem[index + sizeof(line_t)]);
      print_string("\n");
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
  bool error;
  index += strlen(kwd_goto);
  skip_spaces(&index);
  const line_t linenum = get_literal_number(index, &error);
  if (linenum <= 0 || linenum >= MAX_LINENUM || error)
    return print_error("Invalid target line number", initial_index);
  return linenum;
}

/**
 * Check the condition and execute the command if it's met
 */
line_t handle_if(size_t index)
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
    return print_error("Expected 2 expressions for comparison", initial_index);
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
    return print_error("Invalid compare operation", initial_index);
  }

  // Get the second expression
  length = 0;
  while (1) {
    if (codemem[index + length] == '\0')
      return print_error("Expected second expression followed by 'THEN' token", initial_index);
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
}

/**
 * Handle the input command
 */
line_t handle_input(size_t index)
{
  const size_t initial_index = index;

  // Get the target variable
  index += strlen(kwd_input);
  while (isblank(codemem[index]))
    index++;

  if (codemem[index] == '\0')
    return print_error("Expected target variable", initial_index);
  if (!isalpha(codemem[index]) || codemem[index + 1] != '\0')
    return print_error("Expected target variable", initial_index);

  const size_t variable = toupper(codemem[index]) - 'A';

  // Get and exaluate the expression
  size_t expr_length = 0;
  while (1) {
    const char chr = GETCHAR();

    if (chr == '\b') {
      if (expr_length)
        expr_length--;
    }

    else if (chr == '\n') {
      break;
    }

    else if (newline_end + expr_length < CODE_MEMORY_SIZE) {
      codemem[newline_end + expr_length++] = chr;
    }
  }
  codemem[newline_end + expr_length] = '\0';

  bool error;
  var_t expr_value = expr_solve(newline_end, expr_length, &error);
  if (error)
    return MAX_LINENUM;

  variables[variable] = expr_value;
  return 0;
}

#if POKE_PEEK == 1
/**
 * Poke in memory, change some values
 */
line_t handle_poke(size_t index)
{
  bool error;
  const size_t initial_index = index;
  index += strlen(kwd_poke);
  skip_spaces(&index);

  // Get the first expression
  size_t length = 0;
  while (codemem[index + length] != '\0' && codemem[index + length] != ',')
    length++;
  if (codemem[index + length] == '\0')
    return syntax_error("Expected 2 expressions", initial_index);
  size_t address = expr_solve(index, length, &error);
  if (error)
    return MAX_LINENUM;

  // Get the second expression
  index += length + 1;
  length = 0;
  while (codemem[index + length] != '\0')
    length++;
  peek_t value = expr_solve(index, length, &error);
  if (error)
    return MAX_LINENUM;

  // Do the memory operation
  *(peek_t*)(address) = value;

  return 0;
}

/**
 * Peek into the memory, get some values
 */
line_t handle_peek(size_t index)
{
  bool error;
  const size_t initial_index = index;
  index += strlen(kwd_poke);
  skip_spaces(&index);

  // Get the first expression
  size_t length = 0;
  while (codemem[index + length] != '\0' && codemem[index + length] != ',')
    length++;
  if (codemem[index + length] == '\0')
    return syntax_error("Expected 2 expressions", initial_index);
  size_t address = expr_solve(index, length, &error);
  if (error)
    return MAX_LINENUM;

  // Get the variable target
  index += length + 1;
  skip_spaces(&index);
  if (!isalpha(codemem[index]) || codemem[index + 1] != '\0')
    return syntax_error("Expected targer variable", initial_index);
  size_t variable = toupper(codemem[index]) - 'A';

  // Do the memory operation
  variables[variable] = (var_t)(*(peek_t*)(address));
  return 0;
}
#endif

#if FILE_IO == 1
/**
 * Save the memory contents to a file
 */
void handle_save(size_t index)
{
  // Get the file name
  const size_t initial_index = index;
  index += strlen(kwd_save);
  skip_spaces(&index);

  // Check if the code exists
  if (codemem_end == 0) {
    print_error("No code to be saved", initial_index);
    return;
  }

  // Open a file
  const char *filename = &codemem[index];
  FILE *file = fopen(filename, "w");
  if (ferror(file)) {
    print_error("Failed to open file", initial_index);
    fclose(file);
    return;
  }

  // Write to the file
  size_t mem_index = sizeof(line_t);
  while (mem_index < codemem_end) {
    fprintf(file, "%d %s\n", *(line_t*)(&codemem[mem_index - sizeof(line_t)]), &codemem[mem_index]);
    mem_index += strlen(&codemem[mem_index]) + sizeof(line_t) + 1;
  }

  // Close the file
  fclose(file);
}

/**
 * Load the memory contents from a file
 */
void handle_load(size_t index)
{
  const size_t initial_index = index;
  index += strlen(kwd_load);
  skip_spaces(&index);

  // Get the file contents
  const char *filename = &codemem[index];
  FILE *file = fopen(filename, "r");
  if (ferror(file)) {
    print_error("Failed to open file", initial_index);
    fclose(file);
    return;
  }
  fseek(file, 0, SEEK_END);
  const size_t length = ftell(file) + 1;
  char *file_contents = malloc(length * sizeof(char));
  fseek(file, 0, SEEK_SET);
  fread(file_contents, sizeof(char), length, file);
  fclose(file);

  // Load the lines to the memory in a hacky way (copy to new line buffer and execute)
  size_t file_index = 0;
  while (file_index < length) {

    // Get the line length
    size_t line_length = 0;
    while (line_length < length && file_contents[file_index + line_length] != '\n')
      line_length++;

    // Skip if line doesn't seem to be valid
    if (!isdigit(file_contents[file_index])) {
      file_index += line_length + 1;
      continue;
    }

    // Copy the line and set the pointers
    newline_end = newline_ind + line_length;
    for (size_t i = 0; i < line_length; i++)
      codemem[newline_ind + i] = file_contents[file_index + i];
    codemem[newline_ind + line_length] = '\0';

    // Execute the line
    execute_newline();
    file_index += line_length + 1;
  }

  // Remember to dealloc your pointers
  free(file_contents);
}
#endif

/**
 * Ask for confirmation and if confirmed clear the memory
 */
void handle_new(void)
{
  print_string("Really want to do do this? [Y/n]:");
  if (toupper(GETCHAR()) == 'Y') {
    print_string("\nI did as you said\n");
    for (int i = 0; i < CODE_MEMORY_SIZE; i++)
      codemem[i] = '\0';
    codemem_end = 0;
    newline_ind = 0;
    newline_end = 0;
  } else {
    print_string("\n");
  }
}

/**
 * Start the program execution
 */
void handle_run(void)
{
  // Skip if no code exists
  if (!codemem_end) {
    print_string("No code to run, go write some\n");
    return;
  }

  size_t index = sizeof(line_t);
  current_line = *(line_t*)(&codemem[0]);
  while (1) {

    // Execute a line
    line_t nextline = execute_command(index);
    current_line = nextline;

    // Exit if error occured
    if (nextline == MAX_LINENUM) {
      break;
    }

    // Find the next line index if not given
    else if (!nextline) {
      index += strlen(&codemem[index]) + sizeof(line_t) + 1;
      if (index >= codemem_end)
        break;
    }

    // Find the line index based on the line number
    else {
      index = get_line_index(nextline);
      if (index == codemem_end) {
        print_string("Line ");
        print_unsigned(nextline);
        print_string(" not found.\n");
        break;
      }
    }
  }

  current_line = 0;
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
  const char chr = GETCHAR();

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
  // Initialize the IO
  IO_INIT();

  // Preset the static variables
  codemem_end = 0;
  newline_ind = 0;
  newline_end = 0;
  expr_token_count = 0;
  current_line = 0;

  for (int i = 0; i < 26; i++)
    variables[i] = 0;

  // Show the prompt
  print_string("\nTinyBasic by EPSILON0\nExpression limit: ");
  print_unsigned(EXPR_MAX_TOKENS);
  print_string(" tokens\nCode memory: ");
  print_unsigned(CODE_MEMORY_SIZE);
  print_string(" bytes\n\n> ");

  // Main loop
  while (1) {
    const bool execute = handle_shell();

    if (execute) {
      execute_newline();
      print_string("> ");
    }
  }

  return 0;
}
