var x = 5;
var y = 10;

console.log(x);  // 5
console.log(y);  // 10

// Test prefix increment
var a = ++x;
console.log(a);  // 6
console.log(x);  // 6

// Test postfix increment
var b = y++;
console.log(b);  // 10
console.log(y);  // 11

// Test prefix decrement
var c = --x;
console.log(c);  // 5
console.log(x);  // 5

// Test postfix decrement
var d = y--;
console.log(d);  // 11
console.log(y);  // 10

// Test in expressions
var result = x + y++;
console.log(result);  // 15 (5 + 10)
console.log(y);       // 11
