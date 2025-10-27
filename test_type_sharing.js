// Test: Two objects with same structure should share type
var p1 = {x: 0, y: 0};
var p2 = {x: 3, y: 4};
var p3 = {x: 1.5, y: 2.5};  // Different types (double instead of int)
console.log("p1.x: " + p1.x);
console.log("p2.y: " + p2.y);
console.log("p3.x: " + p3.x);
