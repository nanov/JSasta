// Test Array(size) builtin function
var arr = Array(5);

// Array should be zeroed
console.log(arr[0]);  // 0
console.log(arr[1]);  // 0
console.log(arr[4]);  // 0

// Assign values
arr[0] = 10;
arr[1] = 20;
arr[4] = 50;

console.log(arr[0]);  // 10
console.log(arr[1]);  // 20
console.log(arr[4]);  // 50

// Test with variable size
var size = 3;
var arr2 = Array(size);
arr2[0] = 100;
arr2[2] = 300;

console.log(arr2[0]);  // 100
console.log(arr2[1]);  // 0 (still zeroed)
console.log(arr2[2]);  // 300
