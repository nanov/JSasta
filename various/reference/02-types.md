# Types

JSasta is a statically-typed language with type inference and function specialization.

## Primitive Types

### Integer Types
- **Signed**: `i8`, `i16`, `i32`, `i64` 
- **Unsigned**: `u8`, `u16`, `u32`, `u64`

### Floating-Point Types
- `f32` - 32-bit floating point
- `f64` - 64-bit floating point

### Other Primitives
- `bool` - Boolean (`true` or `false`)
- `str` - String (struct with pointer and length)
- `void` - No value (function returns only)

### Type Aliases
- `usize` → `u64` (for sizes/indices)
- `int` → `i32` (default integer)

## Structs

User-defined composite types.

```jsasta
struct Point {
    x: i32;
    y: i32;
}

// Create instance
let p = Point{x: 10, y: 20};

// Access fields
let x_val = p.x;
p.y = 30;
```

### Struct Modifiers

**Value Semantics** - Pass by value instead of reference:
```jsasta
value struct Color {
    r: u8;
    g: u8;
    b: u8;
}
```

**Const** - Immutable struct:
```jsasta
const struct Config {
    max_size: i32;
}
```

## Enums

Tagged unions with multiple variants.

```jsasta
// Unit variants
enum Status {
    Active;
    Inactive;
}

// Variants with fields
enum Option {
    Some(value: i32);
    None;
}

// Named fields
enum Result {
    Ok(value: i32, message: str);
    Err(code: i32);
}
```

### Creating Enums
```jsasta
let status = Status.Active;
let opt = Option.Some{value: 42};
let res = Result.Ok{value: 1, message: "Success"};
```

### Pattern Matching
```jsasta
if (opt is Option.Some(let val)) {
    io.println("Value: {}", val);
}
```

## Arrays

Fixed-size or dynamic collections.

```jsasta
// Fixed-size
let arr: i32[5];

// Dynamic (reference)
let dynamic: ref i32[];

// Literal
let numbers = [1, 2, 3, 4, 5];

// Indexing
let first = numbers[0];
numbers[1] = 10;
```

## References

Pointer types with mutability tracking.

```jsasta
// Mutable reference
let r: ref i32;

// Heap allocation
let arr = new i32[100];
arr[0] = 42;
delete arr;
```

## Type Inference

JSasta infers types automatically in most cases.

### Variable Inference
```jsasta
let x = 42;        // i32
let y = 3.14;      // f64
let s = "hello";   // str
let b = true;      // bool
```

### Function Specialization

Functions with untyped parameters are specialized per call site:

```jsasta
function add(a, b) {
    return a + b;
}

let r1 = add(1, 2);         // Specializes add<i32, i32>
let r2 = add(1.5, 2.5);     // Specializes add<f64, f64>
```

The compiler generates multiple versions of the function based on usage.

### Explicit Type Annotations

You can always specify types explicitly:

```jsasta
let x: i32 = 42;
let name: str = "JSasta";

function greet(name: str): void {
    io.println("Hello, {}", name);
}
```

## Type Compatibility

No implicit conversions - types must match exactly. The `ImplicitFrom` and `ImplicitInto` traits enable automatic conversions in specific contexts (like trait resolution), but there are no user-callable conversion methods like `.into()`.

```jsasta
let a: i32 = 42;
let b: i64 = a;  // ERROR: type mismatch

// No explicit conversion syntax exists
// Conversions happen implicitly through trait system when applicable
```
