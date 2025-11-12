# Expressions and Operators

JSasta supports a rich set of expressions and operators.

## Arithmetic Operators

### Binary Operators

| Operator | Description | Example |
|----------|-------------|---------|
| `+` | Addition | `a + b` |
| `-` | Subtraction | `a - b` |
| `*` | Multiplication | `a * b` |
| `/` | Division | `a / b` |
| `%` | Modulo (remainder) | `a % b` |

```jsasta
let sum = 10 + 5;      // 15
let diff = 10 - 5;     // 5
let prod = 10 * 5;     // 50
let quot = 10 / 5;     // 2
let rem = 10 % 3;      // 1
```

### Unary Operators

| Operator | Description | Example |
|----------|-------------|---------|
| `-` | Negation | `-x` |
| `+` | Plus (identity) | `+x` |

```jsasta
let x = 10;
let neg = -x;    // -10
```

### Operator Traits

Arithmetic operators work through the trait system:
- `Add<Rhs>` for `+`
- `Sub<Rhs>` for `-`
- `Mul<Rhs>` for `*`
- `Div<Rhs>` for `/`
- `Rem<Rhs>` for `%`
- `Neg` for unary `-`

## Comparison Operators

| Operator | Description | Returns |
|----------|-------------|---------|
| `==` | Equal to | `bool` |
| `!=` | Not equal to | `bool` |
| `<` | Less than | `bool` |
| `>` | Greater than | `bool` |
| `<=` | Less than or equal | `bool` |
| `>=` | Greater than or equal | `bool` |

```jsasta
let equal = (5 == 5);        // true
let not_equal = (5 != 3);    // true
let less = (3 < 5);          // true
let greater = (5 > 3);       // true
let less_eq = (5 <= 5);      // true
let greater_eq = (5 >= 3);   // true
```

Uses `Eq` trait for `==` and `!=`, `Ord` trait for ordering comparisons.

## Logical Operators

| Operator | Description | Short-circuit |
|----------|-------------|---------------|
| `&&` | Logical AND | Yes |
| `||` | Logical OR | Yes |
| `!` | Logical NOT | N/A |

```jsasta
let and_result = true && false;   // false
let or_result = true || false;    // true
let not_result = !true;           // false
```

### Short-Circuit Evaluation

`&&` and `||` use short-circuit evaluation:

```jsasta
// Right side not evaluated if left is false
if (ptr != null && ptr[0] > 0) {
    // Safe: ptr[0] only accessed if ptr != null
}

// Right side not evaluated if left is true
if (found || search()) {
    // search() only called if found is false
}
```

Both operands must be `bool` or have `ImplicitInto<bool>` trait.

## Bitwise Operators

| Operator | Description | Example |
|----------|-------------|---------|
| `&` | Bitwise AND | `a & b` |
| `|` | Bitwise OR | `a | b` |
| `^` | Bitwise XOR | `a ^ b` |
| `<<` | Left shift | `a << n` |
| `>>` | Right shift | `a >> n` |

```jsasta
let and = 0b1100 & 0b1010;   // 0b1000 (8)
let or = 0b1100 | 0b1010;    // 0b1110 (14)
let xor = 0b1100 ^ 0b1010;   // 0b0110 (6)
let left = 5 << 2;           // 20
let right = 20 >> 2;         // 5
```

Uses `BitAnd`, `BitOr`, `BitXor`, `Shl`, `Shr` traits.

## Assignment Operators

### Simple Assignment

```jsasta
var x = 10;
x = 20;
```

### Compound Assignment

| Operator | Equivalent to |
|----------|---------------|
| `+=` | `x = x + y` |
| `-=` | `x = x - y` |
| `*=` | `x = x * y` |
| `/=` | `x = x / y` |

```jsasta
var count = 10;
count += 5;   // count = 15
count -= 3;   // count = 12
count *= 2;   // count = 24
count /= 4;   // count = 6
```

Uses `AddAssign`, `SubAssign`, `MulAssign`, `DivAssign` traits.

## Increment and Decrement

### Postfix

```jsasta
var x = 5;
let y = x++;  // y = 5, x = 6
let z = x--;  // z = 6, x = 5
```

### Prefix

```jsasta
var x = 5;
let y = ++x;  // y = 6, x = 6
let z = --x;  // z = 5, x = 5
```

## Member Access

Access struct fields or enum variants using `.`:

```jsasta
struct Point {
    x: i32;
    y: i32;
}

let p = Point{x: 10, y: 20};
let x_val = p.x;           // Read field
p.y = 30;                  // Write field

// Enum variant access
let status = Status.Active;
```

## Index Access

Access array elements using `[]`:

```jsasta
let arr = [1, 2, 3, 4, 5];
let first = arr[0];        // 1
arr[2] = 10;               // Modify element
```

Uses `Index<Idx>` trait for read access, `RefIndex<Idx>` for write access.

## Function Calls

Call functions with `()`:

```jsasta
function add(a: i32, b: i32): i32 {
    return a + b;
}

let result = add(10, 20);
```

## Method Calls

Call functions from imported modules:

```jsasta
import io from @io;
io.println("Hello, {}!", "World");
```

## Ternary Operator

Conditional expression:

```jsasta
let max = (a > b) ? a : b;
let category = (age >= 18) ? "adult" : "minor";
```

## Operator Precedence

From highest to lowest:

1. `()` `[]` `.` (grouping, indexing, member access)
2. `++` `--` `!` `-` `+` (prefix/postfix increment/decrement, unary)
3. `*` `/` `%` (multiplication, division, modulo)
4. `+` `-` (addition, subtraction)
5. `<<` `>>` (bit shifts)
6. `<` `<=` `>` `>=` (comparisons)
7. `==` `!=` (equality)
8. `&` (bitwise AND)
9. `^` (bitwise XOR)
10. `|` (bitwise OR)
11. `&&` (logical AND)
12. `||` (logical OR)
13. `? :` (ternary)
14. `=` `+=` `-=` `*=` `/=` (assignment)

## Examples

### Complex Expression
```jsasta
let result = (a + b) * c - d / e;
```

### Combining Operators
```jsasta
var total = 0;
for (var i = 1; i <= 10; i++) {
    total += i * i;
}
```

### Using Logical Operators
```jsasta
if (x > 0 && x < 100 && y > 0 && y < 100) {
    io.println("In bounds");
}
```
