// Type Analysis Test
// This file tests type inference and function specialization

console.log("=== Type Analysis and Specialization Tests ===");

// Test 1: Function with integer parameters
function addInts(a, b) {
  return a + b;
}

var result1 = addInts(5, 3);
console.log("addInts(5, 3) =", result1);

var result2 = addInts(10, 20);
console.log("addInts(10, 20) =", result2);

// Test 2: Function with double parameters (via literals)
function addDoubles(x, y) {
  var sum = x + y;
  return sum;
}

// Note: Without explicit double literals, these will be ints
// This shows the type system working
var result3 = addDoubles(7, 8);
console.log("addDoubles(7, 8) =", result3);

// Test 3: Function that works with different types
// Based on call site, it should infer the type
function double(n) {
  return n + n;
}

var doubled1 = double(5);
console.log("double(5) =", doubled1);

var doubled2 = double(100);
console.log("double(100) =", doubled2);

// Test 4: Function with string concatenation
function greet(name) {
  var greeting = "Hello, " + name;
  return greeting;
}

var msg1 = greet("Alice");
console.log(msg1);

var msg2 = greet("Bob");
console.log(msg2);

// Test 5: Function with conditional return
function abs(n) {
  if (n < 0) {
    return 0 - n;
  }
  return n;
}

console.log("abs(-5) =", abs(0 - 5));
console.log("abs(5) =", abs(5));
console.log("abs(-100) =", abs(0 - 100));

// Test 6: Function with multiple operations
function calculate(x, y) {
  var sum = x + y;
  var product = x * y;
  var difference = x - y;

  console.log("Sum:", sum);
  console.log("Product:", product);
  console.log("Difference:", difference);

  return sum + product - difference;
}

var result = calculate(10, 5);
console.log("Final result:", result);

// Test 7: Nested function calls with type propagation
function square(n) {
  return n * n;
}

function sumOfSquares(a, b) {
  return square(a) + square(b);
}

var sos = sumOfSquares(3, 4);
console.log("sumOfSquares(3, 4) =", sos);

// Test 8: Function with string and number operations
function repeat(text, count) {
  var result = text;
  for (var i = 1; i < count; i = i + 1) {
    result = result + text;
  }
  return result;
}

var repeated = repeat("Hi", 3);
console.log("repeat('Hi', 3) =", repeated);

// Test 9: Boolean return type
function isPositive(n) {
  return n > 0;
}

var check1 = isPositive(5);
var check2 = isPositive(0 - 3);
console.log("isPositive(5) =", check1);
console.log("isPositive(-3) =", check2);

// Test 10: Complex type inference with mixed operations
function compute(a, b, c) {
  var temp1 = a + b;
  var temp2 = temp1 * c;
  var temp3 = temp2 - a;
  return temp3;
}

var computed = compute(2, 3, 4);
console.log("compute(2, 3, 4) =", computed);

// Test 11: Multiple call sites with same function
// This tests if the compiler handles multiple uses correctly
function identity(x) {
  return x;
}

var id1 = identity(42);
var id2 = identity(100);
var id3 = identity(0);

console.log("identity(42) =", id1);
console.log("identity(100) =", id2);
console.log("identity(0) =", id3);

// Test 12: Function with early return
function max(a, b) {
  if (a > b) {
    return a;
  }
  return b;
}

console.log("max(10, 5) =", max(10, 5));
console.log("max(3, 8) =", max(3, 8));
console.log("max(7, 7) =", max(7, 7));

// Test 13: Chained function calls
function increment(n) {
  return n + 1;
}

function decrement(n) {
  return n - 1;
}

var chained = increment(increment(increment(5)));
console.log("increment(increment(increment(5))) =", chained);

var mixed = decrement(increment(decrement(10)));
console.log("decrement(increment(decrement(10))) =", mixed);

// Test 14: Function that modifies and returns
function processValue(val) {
  var modified = val * 2;
  modified = modified + 10;
  modified = modified - 3;
  return modified;
}

var processed = processValue(5);
console.log("processValue(5) =", processed);

// Test 15: Recursive function with type inference
function countdown(n) {
  if (n <= 0) {
    return 0;
  }
  console.log("Counting:", n);
  return countdown(n - 1);
}

console.log("=== Countdown from 5 ===");
countdown(5);

console.log("=== Type Analysis Tests Complete ===");
