// Test variadic external functions
external printf(string, ...):int;
external puts(string):int;

// Test basic printf with different argument counts
printf("Hello, %s!\n", "World");
printf("Number: %d\n", 42);
printf("Multiple: %d %s %d\n", 1, "test", 2);
printf("Just a string\n");

puts("Testing non-variadic puts");
