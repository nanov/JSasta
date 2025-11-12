// Test that strings are immutable (read-only indexing works)
var str = "Hello";
console.log(str[0]);  // H
console.log(str[1]);  // e
console.log(str[4]);  // o

// Test that string concatenation still works
var str2 = str + " World";
console.log(str2);    // Hello World

// Index into concatenated string
console.log(str2[6]);  // W

// Strings remain immutable - attempting to assign would cause error
// str[0] = 'X';  // This would produce an error
