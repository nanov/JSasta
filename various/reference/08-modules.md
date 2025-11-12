# Modules and Imports

JSasta uses a module system for code organization and reuse.

## Import Syntax

Import modules using the `import` statement:

```jsasta
import namespace from "path/to/module";
```

### Importing Built-in Modules

Built-in modules are prefixed with `@`:

```jsasta
import io from @io;
import test from @test;
import debug from @debug;
```

Note: No quotes around built-in module names.

### Importing User Modules

Import user-defined modules using relative or absolute paths:

```jsasta
// Relative import
import utils from "./utils.jsa";
import helpers from "../helpers.jsa";

// From subdirectory
import math from "./lib/math.jsa";
```

## Using Imported Modules

Access exported symbols through the namespace:

```jsasta
import io from @io;

io.println("Hello, World!");
io.print("No newline");
```

## Export Declarations

Export symbols to make them available to other modules:

```jsasta
// Export a function
export function add(a: i32, b: i32): i32 {
    return a + b;
}

// Export a struct
export struct Point {
    x: i32;
    y: i32;
}

// Export an enum
export enum Status {
    Active;
    Inactive;
}
```

## Module Structure

A JSasta module is a single `.jsa` file containing:
- Import declarations (at the top)
- Type definitions (structs, enums)
- Function definitions
- Top-level code

```jsasta
// module.jsa
import io from @io;

export struct Config {
    name: str;
    value: i32;
}

export function process(cfg: Config): void {
    io.println("Processing: {}", cfg.name);
}

// Module-level initialization
io.println("Module loaded");
```

## Built-in Modules

### @io - Input/Output

Provides formatted I/O functions:

```jsasta
import io from @io;

io.println(format: str, ...);   // Print to stdout with newline
io.print(format: str, ...);     // Print to stdout without newline
io.eprintln(format: str, ...);  // Print to stderr with newline
io.eprint(format: str, ...);    // Print to stderr without newline
io.format(format: str, ...): str; // Return formatted string
```

See [Standard Library](09-stdlib.md) for details.

### @debug - Debugging

Provides debugging utilities:

```jsasta
import debug from @debug;

debug.assert(condition: bool);  // Assert condition is true
```

### @test - Testing

Provides test assertion functions:

```jsasta
import test from @test;

test.assert.equals(expected, actual);      // Assert equality
test.assert.not_equals(not_expected, actual); // Assert inequality
test.assert.that(condition: bool, msg: str, ...); // Assert with message
test.assert.false(msg: str, ...);          // Always fail with message
```

## Module Resolution

### Built-in Modules

Built-in modules (starting with `@`) are resolved by the compiler:
- `@io` → Built-in I/O module
- `@debug` → Built-in debug module
- `@test` → Built-in test module

### User Modules

User modules are resolved relative to:
1. The importing file's directory (for relative paths)
2. The project root (for absolute paths)

```jsasta
// In file: src/main.jsa
import utils from "./utils.jsa";        // Looks in src/utils.jsa
import lib from "../lib/helpers.jsa";   // Looks in lib/helpers.jsa
```

## Module Namespaces

Each imported module gets its own namespace:

```jsasta
import io from @io;
import test from @test;

io.println("From io module");
test.assert.equals(1, 1);
```

The namespace name is determined by the import statement, not the module path:

```jsasta
import my_io from @io;  // Namespace is 'my_io'
my_io.println("Hello!");
```

## Circular Dependencies

Circular module dependencies are not supported and will result in a compilation error.

## Module Initialization

Top-level code in modules executes when the module is loaded:

```jsasta
// module.jsa
import io from @io;

io.println("Module initializing...");

export function init(): void {
    io.println("Explicit initialization");
}
```

## Examples

### Simple Module Usage

```jsasta
// math.jsa
export function square(x: i32): i32 {
    return x * x;
}

export function cube(x: i32): i32 {
    return x * x * x;
}
```

```jsasta
// main.jsa
import io from @io;
import math from "./math.jsa";

let sq = math.square(5);
let cb = math.cube(3);

io.println("Square: {}, Cube: {}", sq, cb);
```

### Multiple Imports

```jsasta
import io from @io;
import test from @test;
import debug from @debug;

debug.assert(1 + 1 == 2);
test.assert.equals(2, 1 + 1);
io.println("All checks passed!");
```
