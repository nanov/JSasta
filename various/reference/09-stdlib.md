# Standard Library

JSasta provides built-in modules for common functionality.

## @io Module

The `@io` module provides formatted input/output functions.

### Functions

#### `io.println(format: str, ...): void`

Print formatted output to stdout with a newline.

```jsasta
import io from @io;

io.println("Hello, World!");
io.println("Value: {}", 42);
io.println("Multiple: {} and {}", 10, 20);
```

#### `io.print(format: str, ...): void`

Print formatted output to stdout without a newline.

```jsasta
io.print("Count: ");
io.print("{}", 5);
io.println("");  // Add newline
```

#### `io.eprintln(format: str, ...): void`

Print formatted output to stderr with a newline.

```jsasta
io.eprintln("Error: {}", error_code);
```

#### `io.eprint(format: str, ...): void`

Print formatted output to stderr without a newline.

```jsasta
io.eprint("Warning: ");
io.eprintln("Something went wrong");
```

#### `io.format(format: str, ...): str`

Return a formatted string without printing.

```jsasta
let message = io.format("Result: {}", 42);
// message = "Result: 42"
```

### Format Strings

Format strings use `{}` as placeholders:

```jsasta
io.println("Simple: {}", 42);
io.println("Multiple: {}, {}, {}", 1, 2, 3);
io.println("Mixed types: {}, {}, {}", 42, 3.14, "text");
```

**Rules:**
- Format string must be a string literal
- Number of `{}` placeholders should match the number of arguments
- Extra arguments are ignored (with a warning)
- Missing arguments cause a compile error

**Supported types:**
Any type that implements the `Display` trait:
- Integers: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`
- Floats: `f32`, `f64`
- Boolean: `bool`
- String: `str`

## @debug Module

The `@debug` module provides debugging utilities.

### Functions

#### `debug.assert(condition: bool): void`

Assert that a condition is true. If false, the program exits with an error message showing the file and line number.

```jsasta
import debug from @debug;

debug.assert(x > 0);
debug.assert(count < max_count);
```

**Behavior:**
- If condition is true: continues execution
- If condition is false: prints error and exits

**Example output on failure:**
```
Assertion failed at file.jsa:10
```

## @test Module

The `@test` module provides test assertion functions for unit testing.

### Test Assertions

#### `test.assert.equals(expected, actual): void`

Assert that two values are equal using the `Eq` trait.

```jsasta
import test from @test;

test.assert.equals(42, result);
test.assert.equals("hello", greeting);
test.assert.equals(true, flag);
```

**Behavior:**
- If equal: test passes (continues execution)
- If not equal: test fails (prints detailed message and exits)

**Failure output:**
```
Assertion failed at file.jsa:15
Expected: 42
Actual: 41
```

#### `test.assert.not_equals(not_expected, actual): void`

Assert that two values are not equal.

```jsasta
test.assert.not_equals(0, count);
test.assert.not_equals("", name);
```

**Behavior:**
- If not equal: test passes
- If equal: test fails with detailed message

#### `test.assert.that(condition: bool, msg: str, ...): void`

Assert that a condition is true with an optional custom message.

```jsasta
test.assert.that(x > 0, "x must be positive");
test.assert.that(count < max, "Count {} exceeds max {}", count, max);
```

**Parameters:**
- `condition`: Boolean expression to test
- `msg`: Optional format string for failure message
- `...`: Optional arguments for format string

**Behavior:**
- If condition is true: test passes
- If condition is false: test fails with custom message

#### `test.assert.false(msg: str, ...): void`

Always fails with a formatted message. Useful for unreachable code paths.

```jsasta
if (result is Option.Some(let val)) {
    io.println("Got: {}", val);
} else {
    test.assert.false("Expected Some, got None");
}
```

## Usage Examples

### Basic I/O

```jsasta
import io from @io;

let name = "JSasta";
let version = 1;

io.println("Welcome to {}!", name);
io.println("Version: {}", version);
```

### Debugging

```jsasta
import io from @io;
import debug from @debug;

function divide(a: i32, b: i32): i32 {
    debug.assert(b != 0);  // Prevent division by zero
    return a / b;
}

let result = divide(10, 2);
io.println("Result: {}", result);
```

### Testing

```jsasta
import test from @test;

function add(a: i32, b: i32): i32 {
    return a + b;
}

// Test the function
let result = add(2, 3);
test.assert.equals(5, result);
test.assert.not_equals(4, result);
test.assert.that(result > 0, "Result should be positive");
```

### Error Reporting

```jsasta
import io from @io;

function process(value: i32): i32 {
    if (value < 0) {
        io.eprintln("Error: Negative value {}", value);
        return -1;
    }
    return value * 2;
}
```

### Formatted Strings

```jsasta
import io from @io;

let name = "Alice";
let age = 30;
let score = 95.5;

// Build a message
let msg = io.format("Player: {}, Age: {}, Score: {}", name, age, score);
io.println("Status: {}", msg);
```

## Testing Workflow

JSasta programs can include inline tests using the `@test` module:

```jsasta
import io from @io;
import test from @test;

// Function to test
function factorial(n: i32): i32 {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

// Tests
test.assert.equals(1, factorial(0));
test.assert.equals(1, factorial(1));
test.assert.equals(2, factorial(2));
test.assert.equals(6, factorial(3));
test.assert.equals(24, factorial(4));

io.println("All tests passed!");
```

Run with the test framework to see detailed results.
