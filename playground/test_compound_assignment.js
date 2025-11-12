// Test compound assignment operators

// Test += with integers
var x = 10;
console.log(x);  // 10
x += 5;
console.log(x);  // 15

// Test -= with integers
var y = 20;
console.log(y);  // 20
y -= 7;
console.log(y);  // 13

// Test *= with integers
var z = 4;
console.log(z);  // 4
z *= 3;
console.log(z);  // 12

// Test /= with integers
var w = 20;
console.log(w);  // 20
w /= 4;
console.log(w);  // 5

// Test with doubles
var a = 10.5;
console.log(a);  // 10.5
a += 2.5;
console.log(a);  // 13.0

var b = 20.0;
console.log(b);  // 20.0
b *= 1.5;
console.log(b);  // 30.0

// Test mixed int and double
var c = 10;
console.log(c);  // 10
c += 2.5;
console.log(c);  // 12.5 (should be double now)

// Test compound in expressions
var d = 5;
d += 3;
var e = d + 2;
console.log(e);  // 10
