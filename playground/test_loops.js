// Loop tests

// For loop
console.log("For loop:");
for (var i = 0; i < 5; i = i + 1) {
    console.log("i =", i);
}

// While loop
console.log("While loop:");
var count = 0;
while (count < 3) {
    console.log("count =", count);
    count = count + 1;
}

// Nested loop
console.log("Nested loop:");
for (var outer = 0; outer < 3; outer = outer + 1) {
    for (var inner = 0; inner < 2; inner = inner + 1) {
        console.log("outer =", outer, "inner =", inner);
    }
}

// Loop with condition
var n = 10;
while (n > 0) {
    console.log(n);
    n = n - 1;
}
console.log("Liftoff!");
