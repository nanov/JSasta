// Test: Functions with array parameters
function sumArray(arr, size) {
    var total = 0;
    for (var i = 0; i < size; i++) {
        total += arr[i];
    }
    return total;
}

var numbers = [1, 2, 3, 4, 5];
var sum = sumArray(numbers, 5);
console.log("Sum: " + sum);
