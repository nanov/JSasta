// Test first-class functions

function print(txt) {
    console.log(txt);
}

// Assign function to variable
var a = print;

// Call through variable
a("Hello from variable!");
a("Second call");

// Original function still works
print("Direct call");
