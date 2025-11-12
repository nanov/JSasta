# Traits

JSasta uses a trait system similar to Rust to define shared behavior across types.

## What are Traits?

Traits define a set of methods that types can implement. They enable:
- Operator overloading
- Generic-like behavior through specialization
- Type-safe implicit conversions
- Common interfaces

## Built-in Traits

### Arithmetic Traits

#### `Add<Rhs>` - Addition operator `+`
```jsasta
// Used by: a + b
// Method: fn add(self, rhs: Rhs) -> Output
```

Implemented for: integers, floats, strings

#### `Sub<Rhs>` - Subtraction operator `-`
```jsasta
// Used by: a - b
```

Implemented for: integers, floats

#### `Mul<Rhs>` - Multiplication operator `*`
```jsasta
// Used by: a * b
```

Implemented for: integers, floats

#### `Div<Rhs>` - Division operator `/`
```jsasta
// Used by: a / b
```

Implemented for: integers, floats

#### `Rem<Rhs>` - Remainder operator `%`
```jsasta
// Used by: a % b
```

Implemented for: integers

#### `Neg` - Unary negation operator `-`
```jsasta
// Used by: -x
// Method: fn neg(self) -> Output
```

Implemented for: integers, floats

### Bitwise Traits

#### `BitAnd<Rhs>` - Bitwise AND `&`
```jsasta
// Used by: a & b
```

Implemented for: integers, bool

#### `BitOr<Rhs>` - Bitwise OR `|`
```jsasta
// Used by: a | b
```

Implemented for: integers, bool

#### `BitXor<Rhs>` - Bitwise XOR `^`
```jsasta
// Used by: a ^ b
```

Implemented for: integers, bool

#### `Shl<Rhs>` - Left shift `<<`
```jsasta
// Used by: a << b
```

Implemented for: integers

#### `Shr<Rhs>` - Right shift `>>`
```jsasta
// Used by: a >> b
```

Implemented for: integers

### Comparison Traits

#### `Eq` - Equality comparison
```jsasta
// Used by: a == b, a != b
// Method: fn eq(self, other: Self) -> bool
```

Implemented for: integers, floats, bool, strings, enums (comparing variants)

#### `Ord` - Ordering comparison
```jsasta
// Used by: a < b, a > b, a <= b, a >= b
// Method: fn lt(self, other: Self) -> bool
//         fn le(self, other: Self) -> bool
//         fn gt(self, other: Self) -> bool
//         fn ge(self, other: Self) -> bool
```

Implemented for: integers, floats

### Compound Assignment Traits

#### `AddAssign<Rhs>` - Addition assignment `+=`
```jsasta
// Used by: a += b
```

#### `SubAssign<Rhs>` - Subtraction assignment `-=`
```jsasta
// Used by: a -= b
```

#### `MulAssign<Rhs>` - Multiplication assignment `*=`
```jsasta
// Used by: a *= b
```

#### `DivAssign<Rhs>` - Division assignment `/=`
```jsasta
// Used by: a /= b
```

Implemented for: integers, floats

### Unary Traits

#### `Not` - Logical NOT `!`
```jsasta
// Used by: !x
// Method: fn not(self) -> Output
```

Implemented for: bool

### Collection Traits

#### `Index<Idx>` - Index read access `[]`
```jsasta
// Used by: let x = arr[i]
// Method: fn index(self, idx: Idx) -> Output
```

Implemented for: arrays (with integer indices)

#### `RefIndex<Idx>` - Index write access `[]`
```jsasta
// Used by: arr[i] = value
// Method: fn ref_index(self, idx: Idx) -> ref Output
```

Implemented for: arrays (with integer indices)

#### `Length` - Get collection length
```jsasta
// Method: fn length(self) -> usize
```

Implemented for: arrays, strings

### Conversion Traits

#### `ImplicitFrom<T>` - Implicit conversion from T
```jsasta
// Used internally by compiler for trait resolution
// Method: fn from(value: T) -> Self
```

This trait enables the compiler to automatically convert types when needed for trait matching, but is not directly callable by users.

#### `ImplicitInto<T>` - Implicit conversion to T
```jsasta
// Used internally by compiler for trait resolution
// Method: fn into(self) -> T
```

Dual of `ImplicitFrom`. Used by logical operators to convert operands to `bool`.

### Display Trait

#### `Display` - Format for output
```jsasta
// Used by: io.println, io.print
// Method: fn display(self) -> void
```

Implemented for: integers, floats, bool, strings

The Display trait is automatically called when printing values with `io.println` and similar functions.

### C Interop Trait

#### `CStr` - Convert to C string
```jsasta
// Method: fn c_str(self) -> ref i8
```

Used for interoperability with C functions that expect null-terminated strings.

## How Traits Work

### Operator Resolution

When you use an operator, the compiler looks for the corresponding trait implementation:

```jsasta
let sum = a + b;
// Compiler looks for: Add<TypeOfB> implemented for TypeOfA
```

### Method Dispatch

Trait methods are dispatched based on type:

```jsasta
io.println("{}", 42);      // Uses Display implementation for i32
io.println("{}", 3.14);    // Uses Display implementation for f64
io.println("{}", "text");  // Uses Display implementation for str
```

### Type Parameters

Many traits have type parameters (like `Rhs` in `Add<Rhs>`), allowing operations between different types:

```jsasta
// Add<str> for str enables: str + str -> str
let s = "Hello" + " World";
```

## Trait Implementations

Built-in types automatically have trait implementations. For example:

```jsasta
// i32 implements Add<i32> with Output = i32
let sum: i32 = 5 + 10;

// str implements Add<str> with Output = str
let text: str = "Hello" + " World";

// Arrays implement Index<usize> with appropriate Output
let arr = [1, 2, 3];
let elem = arr[0];  // Uses Index<usize>
```

## Trait Requirements

Some language features require specific traits:

- **Logical operators** (`&&`, `||`): operands must be `bool` or implement `ImplicitInto<bool>`
- **Comparison operators**: require `Eq` or `Ord` trait
- **Array indexing**: requires `Index` (read) or `RefIndex` (write) trait
- **Arithmetic operators**: require corresponding arithmetic traits

## Future: User-Defined Traits

User-defined trait definitions and implementations are planned for future versions:

```jsasta
// Future syntax (not yet implemented)
trait Drawable {
    fn draw(self): void;
}

impl Drawable for Circle {
    fn draw(self): void {
        // implementation
    }
}
```
