# Lexical Structure

This document describes the lexical elements of JSasta.

## Comments

JSasta supports two types of comments:

### Line Comments
Line comments start with `//` and continue until the end of the line.

```jsasta
// This is a line comment
let x = 42;  // This is also a comment
```

### Block Comments
Block comments start with `/*` and end with `*/`. They can span multiple lines.

```jsasta
/* This is a block comment
   that spans multiple lines */
let y = 10;
```

## Identifiers

Identifiers are used to name variables, functions, structs, enums, and other program elements.

**Rules:**
- Must start with a letter (a-z, A-Z) or underscore (_)
- Can contain letters, digits (0-9), and underscores
- Are case-sensitive
- Cannot be a reserved keyword

**Examples:**
```jsasta
x
myVariable
_private
counter42
Point
```

## Keywords

Reserved keywords in JSasta:

`var` `let` `const` `function` `external` `import` `export` `from` `struct` `enum` `ref` `value` `is` `return` `break` `continue` `if` `else` `for` `while` `new` `delete` `true` `false`

Type keywords: `i8` `i16` `i32` `i64` `u8` `u16` `u32` `u64` `str` `int`

## Literals

### Integer Literals
```jsasta
42       // Default: i32
255u8    // Unsigned 8-bit
1000i64  // Signed 64-bit
```

### Floating-Point Literals
```jsasta
3.14
1.5e10   // Scientific notation
```

### String Literals
```jsasta
"Hello, World!"
"Line 1\nLine 2"  // With escape sequences
"\e[31mRed text\e[0m"  // ANSI color codes
```

Escape sequences: 
- `\n` - Newline
- `\t` - Tab
- `\"` - Double quote
- `\\` - Backslash
- `\e` - Escape character (for ANSI codes)

### Boolean Literals
```jsasta
true
false
```

## Operators

### Arithmetic
`+` `-` `*` `/` `%`

### Comparison
`==` `!=` `<` `>` `<=` `>=`

### Logical
`&&` `||` `!`

### Bitwise
`&` `|` `^` `<<` `>>`

### Assignment
`=` `+=` `-=` `*=` `/=`

### Increment/Decrement
`++` `--` (prefix and postfix)

### Other
`.` (member access) `[]` (index) `?:` (ternary) `()` `{}` `:`

## Punctuation

`;` `,` `(` `)` `{` `}` `[` `]` `@` `...` `_`
