// Test basic ternary operator
var x = 5;
var result = x > 3 ? 10 : 20;
console.log(result);

// Test with different types
var y = 2;
var result2 = y > 3 ? 1.5 : 2.5;
console.log(result2);

// Test nested ternary
var z = 15;
var result3 = z > 10 ? (z > 20 ? 100 : 50) : 0;
console.log(result3);

// Test with string concatenation
var age = 18;
var message = age >= 18 ? "Adult" : "Minor";
console.log(message);

// Test in expression
var a = 5;
var b = 10;
var max = a > b ? a : b;
console.log(max);
