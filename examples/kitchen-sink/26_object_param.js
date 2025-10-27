// Test: Objects as function parameters
function distance(p1, p2) {
    var dx = p2.x - p1.x;
    var dy = p2.y - p1.y;
    return dx * dx + dy * dy;
}

var p1 = {x: 0, y: 0};
var p2 = {x: 3, y: 4};
var dist = distance(p1, p2);
console.log("Distance squared: " + dist);
