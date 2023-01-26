# TinyBasic

## This is a simple TinyBASIC interpreter designed for use on microcontrollers.

Language supports the most basic commands like `LET`, `PRINT`, `GOTO`, `IF` and `INPUT`, 26 single-letter variables are available to the programmer, despite these limitations the language is Turing complete allowing to create (almost) any simple program.

Code memory space, expression tokens limit, maximum line number and shell IO communication way can be configured. No printfs are used (except for the debug stuff), that allows to change the communication method to for example microcontroller's UART module.

`POKE` and `PEEK` command can be disabled when used on PC, as they always cause segmentation fault, and `SAVE` and `LOAD` can be disabled when used on a MCU that doesn't have access to file system.

In case infinite loop occures there is a way to kill the execution by sending any key to the console. Execution is stopped and there is no problem with loosing progress having to reset the MCU. (`KILL_IO` must be enabled.)

---

## List of the supported commands:
Keywords and variable names are case-insensitive.
###### BASIC commands
- `LET <variable> = <expression>` <br>Assigns the result of an expression to the given variable, `LET` keyword isn't necessary and format `<var> = <expr>` will also be understood.
- `PRINT <"string"> : <expression>` <br>Prints out strings and expression results to the console, `:` token can be used as a separator to for example put a value after a string, leaving the separator token at the end of the line disables the linefeed that would have been sent at the end of the line.
- `GOTO <line number>` <br>This command jumps to the given line number.
- `IF <expression> <comparison> <expression> THEN <command>` <br>Executes the command after the `THEN` keyword if the comparison result is true, available comparisons are the following: equal (`=`), not equal (`<>`), lower than (`<`) and greater than (`>`)
- `INPUT <variable>` <br>Gets the expression from the console and put it into the specified variable.
- `REM <comment>` <br>Do nothing, only purpose of this command is to store comments
- `CLEAR` <br>Clears the console and homes the cursor.
- `LIST` <br>Lists the program.
- `MEMORY` <br>Shows how much code memory is left.
- `RUN` <br>Starts the program from the first line.
- `NEW` <br>Clears the code memory after confirmation.
###### POKE_PEEK commands
- `POKE <address expression>, <value expression>` <br>Sets the memory at the given address to the given value
- `PEEK <address expression>, <variable>` <br>Gets the memory from the given address and stores it in the given variable.
- `POKEB <address expression>, <value expression>` <br>Same as `POKE` but only accesses `uint8_t` instead of `peek_t`
- `PEEKB <address expression>, <variable>` <br>Same as `POKE` but only accesses `uint8_t` instead of `peek_t`
###### FILE_IO commands
- `SAVE <filename>` <br>Saves memory contents.
- `LOAD <filename>` <br>Loads memory contents.

---

## Expression solving
##### Supported operations
Language interpreter contains a simple expression solver supporting basic integer arythmetic operations like: add (`+`), subtract (`-`), multiply (`*`), divide (`/`) and remainder (`%`). Basic logic functions are also supported: AND (`&`), OR (`|`), XOR (`^`) and NOT (`!`).
##### Operation precedence
| Precedence |          Operation         | Operator(s) |
|:----------:|:--------------------------:|:-----------:|
|      1     | Subexpressions in brackets | ()          |
|      2     | Unary operators            | +, -, !     |
|      3     | Important arythmetics      | *, /, %     |
|      4     | Low importance arythmetics | +, -        |
|      5     | Logic operations           | &, \|, ^    |
##### Literal formats
|     Name    |                   Format                  |               Examples              |
|:-----------:|:-----------------------------------------:|:-----------------------------------:|
| Variable    | Single letter variable name               | `A`, `B`, `c`, `z`, `Z`             |
| Decimal     | Normal value starting with non-zero digit | `0`, `1`, `2`, `420`, `1024`, `911` |
| Hexadecimal | Hex value starting with `0x` prefix       | `0x45`, `0xFF`, `0xDEADBEEF`        |
| Octal       | Value starting with '0' prefix            | `033`, `0377`, `0105`, `00`         |
| Binary      | Value starting with '0b' prefix           | `0b0110`, `0b1001`, `0b01010101`    |

---

## Configuration

##### Defines
- `NEWLINE` - Character that will be interpreted as a new line.
- `BACKSPACE` - Character that will be interpreted as a backspace.
- `CODE_MEMORY_SIZE` - Size of the program memory.
- `EXPR_MAX_TOKENS` - Size of expression solver buffer.
- `MAX_LINUENUM` - Maximum valid line number (not including MAX_LINENUM).
- `POKE_PEEK` - Enable `POKE` and `PEEK` commands.
- `FILE_IO` - Enable `SAVE` and `LOAD` commands.
- `IO_KILL` - Enable breaking the execution if new characters were received during execution.
- `LOOPBACK` - Enable cosole loopback (input characters will be sent back).

##### Data types
- `line_t` - Format in which line number is stored.
- `var_t` - Format in which variables are stored.
- `uvar_t` - Unisgned version of the format in which variables are stored.
- `peek_t` - Format in which `POKE` and `PEEK` accesses memory.

##### IO Defines
In the definitions `x` is the pointer to the data that has to be sent, received or checked.
- `IO_INIT` - Called at the start of `main()` starts up the IO device.
- `PUTCHAR(x)` - Prints the `x` character to the IO device.
- `GETCHAR(x)` - Return character from the IO device.
- `IO_CHECK(x)` - Returns if there are any new characters in IO (used for IO_KILL).

#### Example configs

###### PC
```c
#define NEWLINE           '\n'
#define BACKSPACE         '\b'
#define CODE_MEMORY_SIZE  8192
#define EXPR_MAX_TOKENS   64
#define MAX_LINENUM       10000
#define POKE_PEEK         0
#define FILE_IO           1
#define IO_KILL           0
#define OUTPUT_CRLF       0
#define SHORT_STRING      0
#define LOOPBACK          0

typedef uint16_t          line_t;
typedef int32_t           var_t;
typedef uint32_t          uvar_t;
typedef size_t            peek_t;

#define IO_INIT()         ((void)0)
#define PUTCHAR(x)        (putchar(*x))
#define GETCHAR(x)        (*x = getchar())
#define IO_CHECK(x)       (*x = false)
```

###### AVR (ATmega328)
```c
#define NEWLINE           '\n'
#define BACKSPACE         '\b'
#define CODE_MEMORY_SIZE  512
#define EXPR_MAX_TOKENS   16
#define MAX_LINENUM       10000
#define POKE_PEEK         1
#define FILE_IO           0
#define IO_KILL           1
#define CRLF              1
#define SHORT_STRING      1
#define LOOPBACK          0

typedef uint16_t          line_t;
typedef int32_t           var_t;
typedef uint32_t          uvar_t;
typedef size_t            peek_t;

#define F_CPU 16000000UL
#define __AVR_ATmega328__
#include <avr/io.h>

#define IO_INIT() {              \
  UBRR0 = F_CPU / 8 / 9600 - 1;  \
  UCSR0A = 1<<U2X0;              \
  UCSR0B = 1<<TXEN0 | 1<<RXEN0;  \
}

#define PUTCHAR(x) {               \
  while (!(UCSR0A & (1<<UDRE0)));  \
  UDR0 = *x;                       \
}

#define GETCHAR(x) {              \
  while (!(UCSR0A & (1<<RXC0)));  \
  *x = UDR0;                      \
}

#define IO_CHECK(x) {           \
  *x = !!(UCSR0A & (1<<RXC0));  \
}
```
---

## Example programs

###### Primes generator
```basic
10 REM Primes generator, get all primes up to max prime
20 PRINT "Enter max prime: ":
30 INPUT C
40 A = 2
50 GOTO 1000
60 A = A + 1
70 IF A < C + 1 THEN GOTO 50
80 END
1000 REM Print number if it is prime
1010 B = 2
1020 IF A % B = 0 THEN GOTO 60
1030 B = B + 1
1040 IF B < A / 2 THEN GOTO 1020
1050 PRINT A
1060 GOTO 60
```