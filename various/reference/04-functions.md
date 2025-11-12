# Functions

Functions are the primary way to organize and reuse code in JSasta.

## Function Declaration

Basic function syntax:

```jsasta
function name(param1: Type1, param2: Type2): ReturnType {
    // function body
    return value;
}
```

## Parameters

### Typed Parameters

Parameters can have explicit types:

```jsasta
function add(a: i32, b: i32): i32 {
    return a + b;
}
```

### Type-Inferred Parameters

Parameters can be untyped, and the function will be specialized based on call sites:

```jsasta
function max(a, b) {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}

// Generates specialized versions:
let m1 = max(10, 20);       // max<i32, i32>
let m2 = max(1.5, 2.5);     // max<f64, f64>
```

### Mixed Parameters

You can mix typed and untyped parameters:

```jsasta
function process(data, count: i32) {
    // data's type is inferred
    // count is explicitly i32
}
```

## Return Types

### Explicit Return Types

Specify return type after the parameter list:

```jsasta
function get_count(): i32 {
    return 42;
}
```

### Inferred Return Types

Return types can be inferred from the function body:

```jsasta
function double(x) {
    return x * 2;  // Return type inferred from usage
}
```

### Void Functions

Functions that don't return a value use `void`:

```jsasta
function print_hello(): void {
    io.println("Hello!");
    // No return statement needed
}
```

Or omit the return type entirely:

```jsasta
function print_hello() {
    io.println("Hello!");
}
```

## Function Calls

Call functions with parentheses:

```jsasta
let result = add(10, 20);
print_hello();
```

## Variadic Functions

Functions can accept variable numbers of arguments using `...`:

```jsasta
// In built-in modules like @io
// io.println(format: str, ...): void

io.println("Values: {}, {}, {}", 1, 2, 3);
```

**Note**: User-defined variadic functions are not yet fully supported. Variadic functions are primarily available in built-in modules.

## External Functions

Declare functions implemented in C or other languages:

```jsasta
external function malloc(size: u64): ref void;
external function free(ptr: ref void): void;
external function printf(format: ref i8, ...): i32;
```

External function requirements:
- All parameters must have type annotations
- Must have explicit return type
- No function body

## Function Specialization

When a function has untyped parameters, the compiler generates specialized versions for each unique set of argument types used:

```jsasta
function identity(x) {
    return x;
}

let a = identity(42);        // Generates identity<i32>
let b = identity(3.14);      // Generates identity<f64>
let c = identity("hello");   // Generates identity<str>
```

This provides generic-like behavior without explicit generic syntax.

## Recursion

Functions can call themselves:

```jsasta
function factorial(n: i32): i32 {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}
```

## Top-Level Execution

JSasta programs execute top-level statements in order. There is no required `main` function:

```jsasta
import io from @io;

// This code runs when the program starts
io.println("Starting program...");

let x = 42;
io.println("x = {}", x);
```

## Examples

### Simple Function
```jsasta
function greet(name: str): void {
    io.println("Hello, {}!", name);
}

greet("World");
```

### Generic-Style Function
```jsasta
function swap(a, b) {
    return [b, a];  // Returns array with swapped values
}

let result = swap(1, 2);  // Works with i32
```

### Function with Multiple Return Points
```jsasta
function abs(x: i32): i32 {
    if (x < 0) {
        return -x;
    }
    return x;
}
```
