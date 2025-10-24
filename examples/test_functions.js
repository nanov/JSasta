// Function tests

// Simple function
function greet(name) {
    console.log("Hello,", name);
    return 0;
}

greet("Alice");
greet("Bob");

// Function with arithmetic
function add(a, b) {
    return a + b;
}

var result1 = add(5, 3);
console.log("5 + 3 =", result1);

var result2 = add(10, 20);
console.log("10 + 20 =", result2);

// Function with multiple operations
function calculate(x) {
    var doubled = x * 2;
    var squared = doubled * doubled;
    return squared;
}

console.log("calculate(3) =", calculate(3));
console.log("calculate(4) =", calculate(4));

// Fibonacci
function fib(n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

console.log("Fibonacci numbers:");
for (var i = 0; i < 10; i = i + 1) {
    console.log("fib(", i, ") =", fib(i));
}

// Factorial
function factorial(n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

console.log("factorial(5) =", factorial(5));
console.log("factorial(10) =", factorial(10));
