// Comprehensive test for arrays and string indexing

// Test 1: Basic integer arrays
var numbers = [10, 20, 30, 40, 50];
console.log(numbers[0]);  // 10
console.log(numbers[2]);  // 30
console.log(numbers[4]);  // 50

// Test 2: Arrays with expressions
var base = 5;
var computed = [base * 2, base * 3, base * 4];
console.log(computed[0]);  // 10
console.log(computed[1]);  // 15
console.log(computed[2]);  // 20

// Test 3: String indexing
var message = "JavaScript";
console.log(message[0]);  // J
console.log(message[4]);  // S
console.log(message[9]);  // t

// Test 4: Array indexing with variable
var arr = [100, 200, 300];
var idx = 1;
console.log(arr[idx]);  // 200

// Test 5: Combining arrays with ternary operator
var x = 5;
var result = x > 3 ? [1, 2, 3] : [4, 5, 6];
console.log(result[0]);  // 1
console.log(result[2]);  // 3
