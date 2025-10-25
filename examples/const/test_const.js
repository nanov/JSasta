// Test const keyword

// Test 1: Basic const declaration
const x = 10;
console.log(x);  // 10

// Test 2: Const with var - regular var can be reassigned
var y = 20;
console.log(y);  // 20
y = 30;
console.log(y);  // 30

// Test 3: Try to reassign const - should error
const z = 100;
console.log(z);  // 100
z = 200;  // ERROR: Cannot assign to const variable
console.log(z);  // Should not reach here
