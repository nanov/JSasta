// Test: Type specialization
function identity(x) {
    return x;
}

var i = identity(42);
var d = identity(3.14);
var s = identity("hello");
console.log("Int: " + i);
console.log("Double: " + d);
console.log("String: " + s);
