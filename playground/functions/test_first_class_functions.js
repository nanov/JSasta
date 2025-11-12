function print(txt) {
	console.log(txt);
}

var a = print;
var b = a;
b("a");
