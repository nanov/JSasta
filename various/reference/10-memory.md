# Memory Management

JSasta provides explicit control over memory allocation and lifetime.

## Stack vs Heap

### Stack Allocation

By default, variables are allocated on the stack:

```jsasta
let x = 42;              // Stack allocated
let arr: i32[10];        // Fixed-size array on stack
let p = Point{x: 1, y: 2}; // Struct on stack
```

Stack allocation is:
- Fast
- Automatically cleaned up when scope ends
- Size must be known at compile time

### Heap Allocation

Use `new` for dynamic heap allocation:

```jsasta
let arr = new i32[100];  // Heap allocated array
```

Heap allocation:
- Allows runtime-determined sizes
- Persists beyond scope until explicitly freed
- Requires manual deallocation with `delete`

## The `new` Operator

Allocate memory on the heap:

```jsasta
// Syntax: new Type[size]
let buffer = new i32[1000];
let dynamic = new u8[size];  // size determined at runtime
```

Returns a reference (`ref Type[]`) to the allocated memory.

## The `delete` Operator

Deallocate heap memory:

```jsasta
let arr = new i32[100];
// Use arr...
delete arr;
```

**Important:**
- Always `delete` what you `new`
- Don't use memory after `delete` (undefined behavior)
- Don't `delete` stack-allocated memory
- Don't `delete` the same memory twice

## References

References are pointer types that can be mutable or immutable.

### Reference Syntax

```jsasta
// Mutable reference
let r: ref i32;

// Array reference
let arr_ref: ref i32[];
```

### Reference Usage

References are automatically created by `new` and when passing structs to functions:

```jsasta
// Heap allocation returns a reference
let heap_arr = new i32[10];  // Type: ref i32[]

// Access through reference
heap_arr[0] = 42;
```

## Assignment Semantics

### Copying vs Aliasing

Assignment behavior depends on whether the source is a value or a reference:

```jsasta
struct Point {
    x: i32;
    y: i32;
}

// Assignment from value: creates a copy
let p1 = Point{x: 1, y: 2};
let p2 = p1;  // p2 is a copy of p1
p2.x = 100;
// p1.x is still 1 (separate copy)

// Assignment from reference: creates an alias
let p3 = Point{x: 5, y: 6};
let p4: ref Point = p3;  // p4 is a reference to p3
p4.x = 200;
// p3.x is now 200 (same object)

// Assigning from a reference variable: also creates an alias
let p5 = p4;  // p5 is also a reference to p3 (not a copy!)
p5.x = 300;
// p3.x is now 300 (all three point to same object)
```

**Key rules:**
- If the source is a value → copy is made
- If the source is a `ref` → the new variable is also a reference (alias)
- Once something is a reference, assigning from it creates another reference, not a copy

## Passing Semantics

### Default: Pass by Value

By default, structs are passed by value to functions (a copy is made):

```jsasta
struct Point {
    x: i32;
    y: i32;
}

function modify(p: Point): void {
    p.x = 100;  // Modifies the COPY
}

let point = Point{x: 1, y: 2};
modify(point);
// point.x is still 1 (original unchanged)
```

### Pass by Reference

To modify the original, use `ref` in the parameter:

```jsasta
function modify_ref(p: ref Point): void {
    p.x = 100;  // Modifies the original
}

let point = Point{x: 1, y: 2};
modify_ref(point);  // Pass reference
// point.x is now 100
```

### Value Struct Modifier

The `value` modifier changes how the struct is stored and passed:

```jsasta
value struct Color {
    r: u8;
    g: u8;
    b: u8;
}
```

**Without `value` modifier (default):**
- Struct is stored as a reference internally
- Loaded when accessed
- Can be passed by value (copy) or by reference (using `ref` parameter)

**With `value` modifier:**
- Struct is stored and passed as a direct value (no indirection)
- Always passed by value - **cannot use `ref` parameter**
- Better performance for small types

**When to use `value` modifier:**
- Small structs (a few fields)
- Mathematical types (vectors, colors)
- Types that should always be copied
- When you want to prevent reference passing

**When to omit `value` modifier (default):**
- Large structs
- Structs that need to be passed by reference
- Most general use cases

## Memory Safety

JSasta does not provide automatic memory safety like Rust. You must:

1. **Match new with delete**
   ```jsasta
   let arr = new i32[100];
   // ... use arr ...
   delete arr;
   ```

2. **Don't use after delete**
   ```jsasta
   let arr = new i32[10];
   delete arr;
   // arr[0] = 42;  // UNDEFINED BEHAVIOR - don't do this!
   ```

3. **Don't delete twice**
   ```jsasta
   let arr = new i32[10];
   delete arr;
   // delete arr;  // UNDEFINED BEHAVIOR - don't do this!
   ```

4. **Don't delete stack memory**
   ```jsasta
   let arr: i32[10];
   // delete arr;  // UNDEFINED BEHAVIOR - don't do this!
   ```

## Array Memory Management

### Fixed-Size Arrays (Stack)

```jsasta
let arr: i32[10];  // Stack allocated, fixed size
arr[0] = 42;
// Automatically cleaned up
```

### Dynamic Arrays (Heap)

```jsasta
let size = 100;
let arr = new i32[size];  // Heap allocated, runtime size
arr[0] = 42;
delete arr;  // Must manually delete
```

## Struct Memory Management

### Stack-Allocated Structs

```jsasta
struct Point {
    x: i32;
    y: i32;
}

let p = Point{x: 1, y: 2};  // Stack allocated
// Automatically cleaned up
```

### Heap-Allocated Struct Arrays

```jsasta
let points = new Point[100];  // Array of structs on heap
points[0] = Point{x: 1, y: 2};
delete points;
```

## String Memory Management

The `str` type is a value type containing a pointer and length:

```jsasta
let s1 = "Hello";  // String literal, static memory
let s2 = s1 + " World";  // Concatenation allocates new memory
// s2's memory is managed by the runtime
```

String concatenation using the `+` operator:
- Allocates new memory via `jsasta_alloc_string`
- Memory is heap-allocated
- Should be freed with `jsasta_free_string` (done automatically in some contexts)

## Best Practices

1. **Prefer stack allocation when possible**
   - Faster
   - No manual cleanup needed
   - Use for fixed-size data

2. **Use heap allocation when needed**
   - Runtime-determined sizes
   - Large data structures
   - Data outliving function scope

3. **Pair every `new` with `delete`**
   - Track your allocations
   - Clean up before returning from functions
   - Avoid memory leaks

4. **Use value semantics for small types**
   - Reduces pointer chasing
   - Simpler semantics
   - Better performance for small data

5. **Document ownership**
   - Make it clear who owns heap memory
   - Document when callers should delete
   - Avoid shared ownership without clear rules

## Examples

### Safe Memory Pattern

```jsasta
function process_data(): void {
    let data = new i32[1000];
    
    // Use data...
    for (var i = 0; i < 1000; i = i + 1) {
        data[i] = i * i;
    }
    
    // Clean up before returning
    delete data;
}
```

### Struct with Heap Memory

```jsasta
struct Buffer {
    data: ref i32[];
    size: i32;
}

function create_buffer(size: i32): Buffer {
    let buf = Buffer{
        data: new i32[size],
        size: size
    };
    return buf;
}

function destroy_buffer(buf: Buffer): void {
    delete buf.data;
}

// Usage
let buffer = create_buffer(100);
// ... use buffer ...
destroy_buffer(buffer);
```

### Value Type for Math

```jsasta
value struct Vector2 {
    x: f64;
    y: f64;
}

function add(a: Vector2, b: Vector2): Vector2 {
    return Vector2{x: a.x + b.x, y: a.y + b.y};
}

let v1 = Vector2{x: 1.0, y: 2.0};
let v2 = Vector2{x: 3.0, y: 4.0};
let sum = add(v1, v2);  // Copies passed by value
```
