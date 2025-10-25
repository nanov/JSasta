var WIDTH = 150;
var GENERATIONS = 60;

const ANSI_RESET = "\e[0m";
const ANSI_BLACK = "\e[30m"; // "\033[0;30m";

function applyRule110(left, center, right) {
	var pattern = left * 4 + center * 2 + right;
	var RULE = 110; // 0b01101110
	var res = RULE >> pattern;
	return res & 1;
}

console.log("Rule 110 Cellular Automaton");
console.log("Width: " + WIDTH);
console.log("Generations: " + GENERATIONS);
console.log("=", WIDTH);
console.log();

let prevLeft = 0;
let prevCenter = 0;
let prevRight = 0;

let currentRow = Array(WIDTH);
var wasHit = true;

for (let i = 0; i < WIDTH; i++) {
	var cell = i == WIDTH - 1 ? 1 : 0;
	currentRow[i] = cell;
	printf(cell == 1 ? ANSI_RESET : ANSI_BLACK);
	printf(cell == 1 ? "*" : "·");
}
console.log(ANSI_RESET);

for (let gen = 1; gen < GENERATIONS; gen++) {
	var nextRow = Array(WIDTH);
	for (let i = 0; i < WIDTH; i++) {
		var left = i == 0 ? 0 : currentRow[i - 1];
		var center = currentRow[i];
		var right = i == WIDTH - 1 ? 0 : currentRow[i + 1];
		var nextCell = applyRule110(left, center, right);
		nextRow[i] = nextCell;
		printf(nextCell == 1 ? ANSI_RESET : ANSI_BLACK);
		printf(nextCell == 1 ? "*" : "·");
	}
	currentRow = nextRow;
	console.log(ANSI_RESET);
}
