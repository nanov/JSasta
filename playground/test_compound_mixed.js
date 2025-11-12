// Test compound assignment with mixed types
var c = 10;
console.log(c);  // 10
c += 2.5;
console.log(c);  // Should be 12.5 but the variable is int type

// This should show the limitation: compound assignment with int variable
// The result gets truncated because c is declared as int
