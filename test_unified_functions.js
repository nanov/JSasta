// Test unified function handling - both user and external functions
external printf(string, ...):int;

// User function with full types
function add(x:int, y:int):int {
    return x + y;
}

// User function with specialization
function multiply(a, b) {
    return a * b;
}

// Test calls
printf("User function: 5 + 3 = %d\n", add(5, 3));
printf("Specialized (int): 4 * 6 = %d\n", multiply(4, 6));
printf("Specialized (double): 2.5 * 3.0 = %f\n", multiply(2.5, 3.0));
printf("All tests passed!\n");
