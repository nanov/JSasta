// Test external function declarations WITH parameter names (old syntax)
external puts(txt:string):int;
external printf(format:string):int;

puts("Hello with param names!");
printf("Testing printf with names\n");

for (var i = 0; i < 2; i = i + 1) {
    puts("Iteration");
}
