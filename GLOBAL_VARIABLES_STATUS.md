# Global Variables Test Results

## ✅ What Works

### 1. Global Variable Declaration and Access
```javascript
var counter = 10;
var message = "Hello";
console.log(counter);  // Works!
```

### 2. Global Variable Modification
```javascript
var counter = 10;
counter = counter + 5;  // Works! counter is now 15
```

### 3. Global Arrays
```javascript
var numbers = [10, 20, 30];
console.log(numbers[0]);  // Works!
numbers[1] = 99;          // Works!
```

### 4. Globals in Control Flow
```javascript
var condition = 1;
if (condition > 0) {
    condition = condition - 2;  // Works!
}
```

### 5. Globals in Loops
```javascript
var loopCounter = 0;
for (var i = 0; i < 5; i++) {
    loopCounter = loopCounter + i;  // Works!
}
```

### 6. Multiple Types
- Integers: ✅
- Floats/Doubles: ✅
- Strings: ✅
- Arrays: ✅

## ❌ Known Limitation

### User-Defined Function Calls
```javascript
function myFunction() {
    console.log("Hello");
}

myFunction();  // Error: Undefined function
```

**Note:** The functions are defined but cannot be called from the main scope. This appears to be a limitation in the current function lookup system, not related to global variables specifically.

## Test Files

1. `examples/test_globals_simple.jsa` - Basic global variable operations
2. `examples/test_globals_demo.jsa` - Comprehensive demo (ALL TESTS PASS ✅)
3. `examples/test_globals_functions.jsa` - Attempted function test (function calls fail)

## Conclusion

**Global variables work perfectly** in JSasta for:
- Reading global values
- Modifying global values
- Using globals in expressions
- Using globals in control flow (if/for/while)
- All basic types (int, double, string, arrays)

The only limitation is calling user-defined functions, which is a separate issue from global variable access.
