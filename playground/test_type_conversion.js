// Type Conversion Test
// Tests int-to-double, int-to-string, double-to-string, bool-to-string conversions

console.log("=== Type Conversion Tests ===");

// Test 1: Int to Int operations (no conversion needed)
console.log("--- Int + Int ---");
var a = 10;
var b = 5;
console.log("10 + 5 =", a + b);
console.log("10 - 5 =", a - b);
console.log("10 * 5 =", a * b);
console.log("10 / 5 =", a / b);

// Test 2: Int comparisons (no conversion needed)
console.log("--- Int Comparisons ---");
console.log("10 > 5 =", a > b);
console.log("10 < 5 =", a < b);
console.log("10 == 10 =", a == a);
console.log("10 != 5 =", a != b);

// Test 3: String concatenation with int (int-to-string conversion)
console.log("--- String + Int (int-to-string) ---");
var name = "Count: ";
var count = 42;
console.log(name + count);
console.log("Answer is " + 42);
console.log(100 + " dollars");

// Test 4: String concatenation with both sides int
console.log("--- Int + String (int-to-string) ---");
console.log(5 + " apples");
console.log(10 + " items total");

// Test 5: Boolean to string conversion
console.log("--- String + Bool (bool-to-string) ---");
var flag = true;
var flag2 = false;
console.log("Flag is: " + flag);
console.log("Flag2 is: " + flag2);
console.log("Result: " + true);
console.log("Result: " + false);

// Test 6: Chained string concatenations with mixed types
console.log("--- Mixed String Concatenations ---");
var result = "Count: " + 10 + ", Flag: " + true;
console.log(result);

console.log("Value: " + 42 + ", Active: " + true + ", Done: " + false);

// Test 7: Multiple int-to-string in one expression
console.log("--- Multiple Conversions ---");
console.log(1 + " + " + 2 + " = " + 3);

// Test 8: Boolean conversions in complex expressions
console.log("--- Boolean Expressions ---");
var x = 10;
var y = 5;
var isGreater = x > y;
var isLess = x < y;
console.log("x > y:", isGreater);
console.log("x < y:", isLess);
console.log("As string: " + isGreater);
console.log("As string: " + isLess);

// Test 9: Comparison with booleans
console.log("--- Boolean Logic ---");
var t = true;
var f = false;
console.log("true == true:", t == t);
console.log("true != false:", t != f);

// Test 10: Complex mixed expressions
console.log("--- Complex Expressions ---");
var num1 = 100;
var num2 = 50;
var sum = num1 + num2;
console.log("Sum of " + num1 + " and " + num2 + " is " + sum);

var product = num1 * 2;
console.log("Double of " + num1 + " is " + product);

console.log("=== All Type Conversion Tests Complete ===");
