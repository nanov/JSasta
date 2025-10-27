// Test mixing both syntaxes in one file
external puts(string):int;                    // without param name
external printf(format:string):int;           // with param name

puts("Without param name");
printf("With param name\n");
