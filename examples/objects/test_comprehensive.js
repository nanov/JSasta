// Test object creation with trailing comma
var obj = {
	name: "mitko",
	age: 25,
	lo: Array(2),
};

function mi(o) {
	return o.age;
}

// Test property access
console.log(obj.name);
console.log(obj.age);

// Test property assignment
obj.name = "eli";
obj.age = 30;
obj.lo[0] = 1;

console.log(obj.name);
console.log(mi(obj));
