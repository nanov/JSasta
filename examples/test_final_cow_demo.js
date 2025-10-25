// Final demo: Copy-on-write for strings

// Scenario 1: Multiple reads, no writes - NO copying
var greeting = "Hello";
console.log(greeting[0]);  // H
console.log(greeting[1]);  // e
console.log(greeting[4]);  // o
// greeting still points to original string literal

// Scenario 2: Read then write - copy ONLY on first write
var name = "Alice";
console.log(name[0]);      // A - read, no copy
name[0] = "B";             // COPY happens here
console.log(name[0]);      // B - read from copy
name[4] = "y";             // COPY again (from previous copy)
console.log(name[4]);      // y

// Scenario 3: Arrays work as before (no COW needed)
var nums = [10, 20, 30];
console.log(nums[0]);      // 10
nums[0] = 99;              // Direct modification
console.log(nums[0]);      // 99

// Scenario 4: String concatenation still creates new strings
var msg = "Hi" + " there";
console.log(msg[0]);       // H
msg[0] = "J";              // Copy on write
console.log(msg[0]);       // J
