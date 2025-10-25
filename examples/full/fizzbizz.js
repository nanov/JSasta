const INTERATIONS = 15;

for (var i = 1; i <= INTERATIONS; i++) {
	if (i % 15 === 0) printf("FizzBuzz");
	else if (i % 3 === 0) printf("Fizz");
	else if (i % 5 === 0) printf("Buzz");
	else printf("%d", i);
	printf(" ");
}

console.log();
