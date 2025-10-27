// Test variadic external functions without parameter names
external printf(string, ...):int;
external sprintf(string, string, ...):int;

printf("Testing variadic without param names: %d %s\n", 123, "works");
