// Comprehensive ternary operator test

function testTernary(x) {
    return x > 10 ? 100 : 50;
}

function max(a, b) {
    return a > b ? a : b;
}

function checkAge(age) {
    return age >= 18 ? 1 : 0;
}

// Test the functions
console.log(testTernary(15));
console.log(testTernary(5));
console.log(max(10, 20));
console.log(max(30, 15));
console.log(checkAge(21));
console.log(checkAge(16));
