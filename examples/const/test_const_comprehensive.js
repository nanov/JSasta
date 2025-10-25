// Comprehensive const test

// Constants can be declared and used
const PI = 3.14159;
const MAX_SIZE = 100;
const MESSAGE = "Hello, World!";

console.log(PI);
console.log(MAX_SIZE);
console.log(MESSAGE);

// Constants work in expressions
const radius = 5;
const area = PI * radius * radius;
console.log(area);  // ~78.53975

// Mix of const and var
const immutable = 42;
var mutable = 10;

console.log(immutable);  // 42
console.log(mutable);    // 10

// var can be modified
mutable = 20;
console.log(mutable);    // 20

// var can use increment
mutable++;
console.log(mutable);    // 21

// var can use compound assignment
mutable += 5;
console.log(mutable);    // 26

// Const values work in calculations
const x = 10;
const y = 20;
const sum = x + y;
const product = x * y;

console.log(sum);      // 30
console.log(product);  // 200

// Const with different types
const intConst = 100;
const doubleConst = 2.5;
const stringConst = "test";

console.log(intConst);
console.log(doubleConst);
console.log(stringConst);
