function na(name) {
	return "A" + name;
}
function meet(name) {
	var greeting = "Meet," + na(name);
	return greeting;
}

var a = "nitko";
var b = meet(a);

console.log(b);
