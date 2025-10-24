function meet(name) {
  var greeting = "Meet," + name;
  return greeting;
}

function greet(name) {
  var greeting = "Hello, " + name + " and " + meet(name);
  return greeting;
}

console.log(greet("mitko"));
