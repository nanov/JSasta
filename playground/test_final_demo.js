// Final demo: arrays, strings, indexing, and assignment

// Create and modify an array
var scores = [85, 90, 78, 92, 88];
console.log(scores[0]);  // 85
scores[0] = 95;
console.log(scores[0]);  // 95

// Create and modify a string
var name = "Alice";
console.log(name[0]);  // A
name[0] = "E";
console.log(name[0]);  // E

// Use ternary with arrays
var x = 10;
var arr = x > 5 ? [1, 2, 3] : [4, 5, 6];
console.log(arr[0]);  // 1
arr[2] = 30;
console.log(arr[2]);  // 30

// Variable index
var data = [100, 200, 300, 400];
var i = 2;
console.log(data[i]);  // 300
data[i] = 350;
console.log(data[i]);  // 350
