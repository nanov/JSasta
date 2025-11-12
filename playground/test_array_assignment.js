// Test array assignment by index
var arr = [10, 20, 30, 40, 50];
console.log(arr[0]);  // 10
console.log(arr[2]);  // 30

// Modify array elements
arr[0] = 100;
arr[2] = 300;

console.log(arr[0]);  // 100
console.log(arr[2]);  // 300

// Test with variable index
var idx = 4;
arr[idx] = 500;
console.log(arr[4]);  // 500

// Test with expression
arr[1 + 1] = 250;
console.log(arr[2]);  // 250
