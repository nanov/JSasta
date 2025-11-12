# Control Flow

JSasta provides several control flow constructs for conditional execution and loops.

## If Expressions

### Basic If

```jsasta
if (condition) {
    // code if true
}
```

### If-Else

```jsasta
if (condition) {
    // code if true
} else {
    // code if false
}
```

### If-Else Chains

```jsasta
if (x < 0) {
    io.println("negative");
} else if (x == 0) {
    io.println("zero");
} else {
    io.println("positive");
}
```

### Condition Requirements

The condition must be a boolean expression:

```jsasta
let x = 42;
if (x) {  // ERROR: x is i32, not bool
    // ...
}

if (x > 0) {  // OK: comparison returns bool
    // ...
}
```

## While Loops

Execute code repeatedly while a condition is true:

```jsasta
var i = 0;
while (i < 10) {
    io.println("{}", i);
    i = i + 1;
}
```

## For Loops

Iterate over a range or collection:

```jsasta
for (var i = 0; i < 10; i = i + 1) {
    io.println("{}", i);
}
```

The for loop syntax:
```jsasta
for (initialization; condition; increment) {
    // loop body
}
```

## Pattern Matching

Use the `is` operator to match enum variants and bind fields:

### Basic Pattern Matching

```jsasta
enum Option {
    Some(value: i32);
    None;
}

let opt = Option.Some{value: 42};

if (opt is Option.Some(let value)) {
    io.println("Got value: {}", value);
}
```

### Multiple Bindings

```jsasta
enum Result {
    Ok(value: i32, message: str);
    Err(code: i32);
}

let res = Result.Ok{value: 1, message: "Success"};

if (res is Result.Ok(let val, let msg)) {
    io.println("Value: {}, Message: {}", val, msg);
}
```

### Pattern Variables

Use `let` to bind matched fields:
- `let varname` - binds the field to a new variable
- `_` - wildcard, ignores the field

```jsasta
if (opt is Option.Some(let x)) {
    // x is bound to the value field
}

if (res is Result.Ok(let _, let msg)) {
    // First field ignored, msg is bound
}
```

## Break and Continue

### Break

Exit a loop early:

```jsasta
var i = 0;
while (true) {
    if (i >= 10) {
        break;
    }
    io.println("{}", i);
    i = i + 1;
}
```

### Continue

Skip to the next iteration:

```jsasta
for (var i = 0; i < 10; i = i + 1) {
    if (i % 2 == 0) {
        continue;  // Skip even numbers
    }
    io.println("{}", i);
}
```

## Return

Exit a function and optionally return a value:

```jsasta
function find_positive(arr: ref i32[]): i32 {
    for (var i = 0; i < 10; i = i + 1) {
        if (arr[i] > 0) {
            return arr[i];  // Early return
        }
    }
    return -1;  // Not found
}
```

## Ternary Operator

Conditional expression using `? :`:

```jsasta
let max = (a > b) ? a : b;
let sign = (x >= 0) ? "positive" : "negative";
```

## Block Expressions

Blocks can be used as expressions (though return value handling may be limited):

```jsasta
let result = {
    let temp = compute_value();
    temp * 2
};
```

## Examples

### Loop with Break
```jsasta
var count = 0;
while (count < 100) {
    if (count * count > 50) {
        break;
    }
    count = count + 1;
}
io.println("Final count: {}", count);
```

### Pattern Matching Chain
```jsasta
if (result is Result.Ok(let val, let _)) {
    io.println("Success: {}", val);
} else if (result is Result.Err(let code)) {
    io.println("Error code: {}", code);
}
```

### Nested Loops with Continue
```jsasta
for (var i = 0; i < 5; i = i + 1) {
    for (var j = 0; j < 5; j = j + 1) {
        if (i == j) {
            continue;
        }
        io.println("({}, {})", i, j);
    }
}
```
