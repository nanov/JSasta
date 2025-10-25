// JSasta Syntax Example File
// This file demonstrates all syntax highlighting features

// Variable declarations with type annotations
var count: int = 42;
var temperature: double = 98.6;
var message: string = "Hello, JSasta!";
var isActive: bool = true;

// Function with parameter and return type annotations
function calculateArea(width: int, height: int): int {
    return width * height;
}

// Object with type annotation
var person: { name: string, age: int, active: bool } = {
    name: "Alice",
    age: 30,
    active: true,
};

// Function with object parameter
function displayPerson(p: { name: string, age: int }): string {
    return p.name + " is " + p.age + " years old";
}

// Control flow
if (count > 0) {
    console.log("Positive number");
} else {
    console.log("Non-positive number");
}

// Loops
while (count < 100) {
    count++;
}

for (var i: int = 0; i < 10; i++) {
    console.log(i);
}

// Function call
var area = calculateArea(10, 20);
console.log(area);

// Member access
console.log(person.name);
console.log(person.age);

// Arrays (inferred types)
var numbers = [1, 2, 3, 4, 5];
var names = ["Alice", "Bob", "Charlie"];

// Operators
var sum = 10 + 20;
var product = 5 * 4;
var comparison = (sum > product);
var bitshift = 8 >> 2;

/* Multi-line comment
   This demonstrates block comments
   which can span multiple lines
*/

// Object assignment
var config = {
    port: 8080,
    host: "localhost",
    debug: true,
};

// Nested member access
console.log(config.port);

// Return statement
function getValue(): int {
    return 42;
}
