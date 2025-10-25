// Test const enforcement with various operations

// Test 1: Try to use ++ on const
const a = 10;
a++;  // ERROR: Cannot modify const variable

// Test 2: Try to use -- on const
const b = 20;
--b;  // ERROR: Cannot modify const variable

// Test 3: Try compound assignment on const
const c = 30;
c += 5;  // ERROR: Cannot assign to const variable

console.log("Should not reach here");
