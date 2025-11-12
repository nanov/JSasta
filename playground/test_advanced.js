// Advanced JavaScript Compiler Test
// This file demonstrates all supported features

// ========== Variables and Types ==========
console.log("=== Variables and Types ===");

var intVar = 42;
var doubleVar = 3.14;
var stringVar = "Hello, World!";
var boolVar = true;

console.log("Integer:", intVar);
console.log("Double:", doubleVar);
console.log("String:", stringVar);
console.log("Boolean:", boolVar);

// ========== Arithmetic Operations ==========
console.log("=== Arithmetic Operations ===");

var a = 10;
var b = 3;

console.log("Addition:", a + b);
console.log("Subtraction:", a - b);
console.log("Multiplication:", a * b);
console.log("Division:", a / b);

// Unary operators
var negative = -a;
console.log("Negation:", negative);

// ========== String Operations ==========
console.log("=== String Operations ===");

var str1 = "Hello";
var str2 = "World";
var combined = str1 + " " + str2;
console.log("Concatenation:", combined);

// String with number
var count = 5;
var message = "Count is " + count;
console.log(message);

// ========== Comparison Operators ==========
console.log("=== Comparison Operators ===");

var x = 10;
var y = 20;

console.log("10 < 20:", x < y);
console.log("10 > 20:", x > y);
console.log("10 <= 20:", x <= y);
console.log("10 >= 20:", x >= y);
console.log("10 == 20:", x == y);
console.log("10 != 20:", x != y);

// ========== Logical Operators ==========
console.log("=== Logical Operators ===");

var t = true;
var f = false;

console.log("true && false:", t && f);
console.log("true || false:", t || f);
console.log("!true:", !t);
console.log("!false:", !f);

// ========== If Statements ==========
console.log("=== If Statements ===");

var score = 85;

if (score >= 90) {
    console.log("Grade: A");
} else {
    if (score >= 80) {
        console.log("Grade: B");
    } else {
        console.log("Grade: C");
    }
}

// ========== While Loops ==========
console.log("=== While Loops ===");

var counter = 0;
while (counter < 5) {
    console.log("Counter:", counter);
    counter = counter + 1;
}

// ========== For Loops ==========
console.log("=== For Loops ===");

for (var i = 0; i < 5; i = i + 1) {
    console.log("Loop iteration:", i);
}

// Nested loop
console.log("Multiplication table (3x3):");
for (var row = 1; row <= 3; row = row + 1) {
    for (var col = 1; col <= 3; col = col + 1) {
        var product = row * col;
        console.log(row, "x", col, "=", product);
    }
}

// ========== Functions ==========
console.log("=== Functions ===");

// Simple function
function greet(name) {
    console.log("Hello,", name);
    return 0;
}

greet("Alice");
greet("Bob");

// Function with return value
function square(n) {
    return n * n;
}

console.log("square(5) =", square(5));
console.log("square(7) =", square(7));

// Function with multiple parameters
function add(a, b) {
    return a + b;
}

function subtract(a, b) {
    return a - b;
}

function multiply(a, b) {
    return a * b;
}

console.log("add(10, 5) =", add(10, 5));
console.log("subtract(10, 5) =", subtract(10, 5));
console.log("multiply(10, 5) =", multiply(10, 5));

// ========== Recursive Functions ==========
console.log("=== Recursive Functions ===");

// Factorial
function factorial(n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

console.log("factorial(0) =", factorial(0));
console.log("factorial(1) =", factorial(1));
console.log("factorial(5) =", factorial(5));
console.log("factorial(10) =", factorial(10));

// Fibonacci
function fib(n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

console.log("Fibonacci sequence:");
for (var k = 0; k <= 10; k = k + 1) {
    console.log("fib(", k, ") =", fib(k));
}

// ========== Complex Example ==========
console.log("=== Complex Example: Power Function ===");

function power(base, exp) {
    if (exp == 0) {
        return 1;
    }
    if (exp == 1) {
        return base;
    }
    return base * power(base, exp - 1);
}

console.log("2^0 =", power(2, 0));
console.log("2^5 =", power(2, 5));
console.log("3^4 =", power(3, 4));
console.log("5^3 =", power(5, 3));

// ========== Prime Number Check ==========
console.log("=== Prime Number Check ===");

function isPrime(n) {
    if (n <= 1) {
        return false;
    }
    if (n <= 3) {
        return true;
    }
    
    var i = 2;
    while (i * i <= n) {
        if (n / i * i == n) {
            return false;
        }
        i = i + 1;
    }
    return true;
}

console.log("Checking primes from 1 to 20:");
for (var num = 1; num <= 20; num = num + 1) {
    if (isPrime(num)) {
        console.log(num, "is prime");
    }
}

// ========== Sum of Series ==========
console.log("=== Sum of Series ===");

function sumToN(n) {
    var sum = 0;
    for (var i = 1; i <= n; i = i + 1) {
        sum = sum + i;
    }
    return sum;
}

console.log("Sum 1 to 10:", sumToN(10));
console.log("Sum 1 to 100:", sumToN(100));

console.log("=== All tests completed successfully! ===");
