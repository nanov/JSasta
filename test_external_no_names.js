// Test external function declarations without parameter names
external puts(string):int;
external printf(string):int;

puts("Hello from external function without param names!");
printf("Testing printf too\n");

for (var i = 0; i < 3; i = i + 1) {
    puts("Loop iteration");
}
