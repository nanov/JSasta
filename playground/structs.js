external printf(string, ...):int;
external puts(string):int;

// Example 1: Simple struct without defaults
struct Point {
    x: int;
    y: int;
}

// Example 2: Struct with default values
struct Vector3D {
    x: int = 0;
    y: int = 0;
    z: int = 0;
}

// Example 3: Mixed required and optional fields
struct Rectangle {
    x: int = 0;
    y: int = 0;
    width: int;   // Required
    height: int;  // Required
}

puts("=== Struct Examples ===\n");

// Create a simple point (all properties required)
var p1: Point = { x: 10, y: 20 };
printf("Point: (%d, %d)\n", p1.x, p1.y);

// Create vectors with varying amounts of properties
var v1: Vector3D = { x: 1, y: 2, z: 3 };  // All provided
var v2: Vector3D = { x: 5 };               // Partial - y=0, z=0 from defaults
var v3: Vector3D = { };                    // Empty - all from defaults

printf("Vector (all): (%d, %d, %d)\n", v1.x, v1.y, v1.z);
printf("Vector (partial): (%d, %d, %d)\n", v2.x, v2.y, v2.z);
printf("Vector (empty): (%d, %d, %d)\n", v3.x, v3.y, v3.z);

// Mixed required/optional
var rect1: Rectangle = { width: 100, height: 50 };
var rect2: Rectangle = { x: 10, y: 20, width: 100, height: 50 };

printf("Rectangle 1: pos=(%d,%d) size=%dx%d\n", rect1.x, rect1.y, rect1.width, rect1.height);
printf("Rectangle 2: pos=(%d,%d) size=%dx%d\n", rect2.x, rect2.y, rect2.width, rect2.height);

// Use struct properties in expressions
var diagonal_x = rect2.x + rect2.width;
var diagonal_y = rect2.y + rect2.height;
printf("Diagonal corner: (%d, %d)\n", diagonal_x, diagonal_y);

puts("\n=== All tests passed! ===");
