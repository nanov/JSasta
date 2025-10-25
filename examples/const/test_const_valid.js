// Test const keyword - valid usage

// Test 1: Basic const declaration
const x = 10;
console.log(x);  // 10

// Test 2: Const with expressions
const y = 5 + 3;
console.log(y);  // 8

// Test 3: Multiple consts
const a = 100;
const b = 200;
const c = a + b;
console.log(c);  // 300

// Test 4: Const with different types
const myInt = 42;
const myDouble = 3.14;
const myString = "hello";
console.log(myInt);
console.log(myDouble);
console.log(myString);

// Test 5: Regular var can be reassigned
var mutable = 10;
console.log(mutable);  // 10
mutable = 20;
console.log(mutable);  // 20
