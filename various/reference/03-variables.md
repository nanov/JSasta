# Variables and Constants

JSasta provides three ways to declare bindings: `var`, `let`, and `const`.

## Variable Declarations

### `let` - Immutable Variables

The `let` keyword declares an immutable variable binding:

```jsasta
let x = 42;
let name = "JSasta";
```

Once assigned, the value cannot be changed:

```jsasta
let x = 10;
x = 20;  // ERROR: cannot reassign to immutable variable
```

### `var` - Mutable Variables

The `var` keyword declares a mutable variable:

```jsasta
var counter = 0;
counter = counter + 1;  // OK
counter = 10;           // OK
```

### `const` - Compile-Time Constants

The `const` keyword declares compile-time constants (not yet fully implemented):

```jsasta
const MAX_SIZE = 100;
const PI = 3.14159;
```

## Type Annotations

Variables can have explicit type annotations:

```jsasta
let x: i32 = 42;
var count: i64 = 0;
let name: str = "JSasta";
```

## Type Inference

When types are not explicitly specified, they are inferred from the initializer:

```jsasta
let x = 42;           // Inferred as i32
let y = 3.14;         // Inferred as f64
let s = "hello";      // Inferred as str
let b = true;         // Inferred as bool
let arr = [1, 2, 3];  // Inferred as i32[] array
```

## Initialization

All variables must be initialized at declaration:

```jsasta
let x;  // ERROR: missing initializer
```

## Scope

Variables are scoped to the block in which they are declared:

```jsasta
{
    let x = 10;
    io.println("{}", x);  // OK
}
io.println("{}", x);  // ERROR: x is not in scope
```

### Function Scope

Variables declared in a function are scoped to that function:

```jsasta
function example() {
    let local = 42;
    // local is accessible here
}
// local is NOT accessible here
```

### Shadowing

Variables can shadow outer scope variables:

```jsasta
let x = 10;
{
    let x = 20;  // Shadows outer x
    io.println("{}", x);  // Prints 20
}
io.println("{}", x);  // Prints 10
```

## Naming Conventions

By convention:
- Use `snake_case` for variable names: `user_count`, `max_value`
- Use `PascalCase` for type names: `Point`, `Status`
- Use `SCREAMING_SNAKE_CASE` for constants: `MAX_SIZE`, `DEFAULT_PORT`
