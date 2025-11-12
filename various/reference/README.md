# JSasta Language Reference

JSasta is a statically-typed, compiled programming language with a trait-based type system. It compiles to native code via LLVM.

## Table of Contents

1. [Lexical Structure](01-lexical-structure.md)
   - Comments
   - Identifiers
   - Keywords
   - Literals
   - Operators

2. [Types](02-types.md)
   - Primitive Types
   - Structs
   - Enums
   - Arrays
   - References
   - Type Aliases

3. [Variables and Constants](03-variables.md)
   - Variable Declarations
   - Constant Declarations
   - Mutability
   - Scope

4. [Functions](04-functions.md)
   - Function Declarations
   - Parameters and Return Types
   - Variadic Functions
   - External Functions

5. [Control Flow](05-control-flow.md)
   - If Expressions
   - While Loops
   - For Loops
   - Pattern Matching
   - Break and Continue

6. [Expressions and Operators](06-expressions.md)
   - Arithmetic Operators
   - Comparison Operators
   - Logical Operators
   - Bitwise Operators
   - Assignment Operators
   - Member Access
   - Index Access
   - Ternary Operator

7. [Traits](07-traits.md)
   - Trait System Overview
   - Built-in Traits
   - Trait Implementations

8. [Modules and Imports](08-modules.md)
   - Module System
   - Import Declarations
   - Export Declarations
   - Built-in Modules

9. [Standard Library](09-stdlib.md)
   - @io Module
   - @debug Module
   - @test Module

10. [Memory Management](10-memory.md)
    - Stack vs Heap
    - new and delete
    - Value Semantics
    - References

## Quick Start Example

```jsasta
// Import the io module for printing
import io from @io;

// Define a struct
struct Point {
    x: i32;
    y: i32;
}

// Define a function
function distance(p1: Point, p2: Point): i32 {
    let dx = p1.x - p2.x;
    let dy = p1.y - p2.y;
    return dx * dx + dy * dy;
}

// Top-level code execution (no main function needed)
let p1 = Point{x: 0, y: 0};
let p2 = Point{x: 3, y: 4};

let dist = distance(p1, p2);
io.println("Distance: {}", dist);
```

## Language Philosophy

- **Static Typing**: All types are known at compile time
- **Trait-Based**: Behavior is defined through traits, similar to Rust
- **Explicit**: No hidden conversions or implicit behaviors
- **Value Semantics**: By default, structs are passed by reference; use `value` modifier for pass-by-value
- **Memory Safety**: Explicit memory management with `new` and `delete`
