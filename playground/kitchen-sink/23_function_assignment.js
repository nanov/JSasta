// Test: Function assignment (first-class functions)

function greet(name) {
	console.log("Hello, " + name);
}

// Assign function to variable
var sayHello = greet;

// Call through variable
sayHello("World");
sayHello("Alice");

// Direct call still works
greet("Bob");
