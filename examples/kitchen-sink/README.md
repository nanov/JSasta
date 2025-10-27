# Kitchen Sink Tests - Feature Status

This directory contains individual test files for each JSasta compiler feature.

## ✅ PASSING TESTS (27/27)

### Primitive Types & Literals
- ✅ `01_integers.js` - Integer literals and variables
- ✅ `02_doubles.js` - Double/float literals
- ✅ `03_strings.js` - String literals and concatenation
- ✅ `04_booleans.js` - Boolean literals

### Operators
- ✅ `05_arithmetic.js` - Arithmetic operators (+, -, *, /, %)
- ✅ `06_bitwise.js` - Bitwise AND (&) and right shift (>>)
- ✅ `07_comparison.js` - Comparison operators (==, !=, <, >, <=, >=)
- ✅ `08_logical.js` - Logical operators (&&, ||, !)
- ✅ `09_ternary.js` - Ternary operator (? :)

### Variables & Assignment
- ✅ `10_var_let_const.js` - var, let, const declarations
- ✅ `11_compound_assignment.js` - Compound assignments (+=, -=, *=, /=)
- ✅ `12_increment_decrement.js` - Increment/decrement (++, --)

### Control Flow
- ✅ `13_if_else.js` - if/else statements
- ✅ `14_for_loop.js` - for loops
- ✅ `15_while_loop.js` - while loops
- ✅ `23_nested_loops.js` - Nested loops

### Arrays
- ✅ `16_array_literal.js` - Array literals [1, 2, 3]
- ✅ `17_array_constructor.js` - Array(size) constructor
- ✅ `18_array_assignment.js` - Array element assignment

### Strings
- ✅ `19_string_indexing.js` - String character access

### Functions
- ✅ `20_function_basic.js` - Basic function declarations
- ✅ `21_function_recursive.js` - Recursive functions
- ✅ `22_function_array_param.js` - Functions with array parameters
- ✅ `27_type_specialization.js` - Type-based function specialization

### Objects
- ✅ `24_objects.js` - Object literals
- ✅ `25_object_assignment.js` - Object property assignment
- ✅ `26_object_param.js` - Objects as function parameters

## ❌ NOT IMPLEMENTED

The following features are not yet implemented:
- Left shift operator (<<)
- Bitwise OR operator (|)
- Bitwise XOR operator (^)

## Running Tests

To test all features:
```bash
cd /Users/dimitarnanov/work/private/github/JSasta
./test_all.sh
```

To test individual features:
```bash
./build/jsastac examples/kitchen-sink/01_integers.js
```

## Summary

**Working**: 27/27 tests (100% ✓)
- All primitive types work
- All basic operators work (except left shift, OR, XOR - not implemented)
- All control flow works
- Arrays work completely
- Functions work including recursion and specialization
- Objects work completely
