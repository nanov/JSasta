// Comprehensive test for array and string assignment

// Test 1: Array creation and modification
var numbers = [1, 2, 3, 4, 5];
console.log(numbers[0]);  // 1
console.log(numbers[4]);  // 5

numbers[0] = 10;
numbers[4] = 50;
console.log(numbers[0]);  // 10
console.log(numbers[4]);  // 50

// Test 2: String creation and modification
var greeting = "Hello";
console.log(greeting[0]);  // H
console.log(greeting[4]);  // o

greeting[0] = "J";
greeting[4] = "y";
console.log(greeting[0]);  // J
console.log(greeting[4]);  // y

// Test 3: Variable index
var arr = [100, 200, 300];
var idx = 1;
arr[idx] = 250;
console.log(arr[1]);  // 250

// Test 4: Expression as index
arr[0 + 2] = 350;
console.log(arr[2]);  // 350

// Test 5: Multiple modifications
var text = "abcde";
text[0] = "A";
text[1] = "B";
text[2] = "C";
console.log(text[0]);  // A
console.log(text[1]);  // B
console.log(text[2]);  // C
