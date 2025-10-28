# Root Cause Analysis - Global Variables in Functions

## The Problem
```javascript
var G = 0;

function print():void {
    console.log(G);  // ERROR: Undefined variable: G
}

print();
```

## Root Cause Found ✅

### Order of Operations in codegen_generate()

**Current order:**
1. Line 1615-1719: Generate ALL function bodies
2. Line 1723-1740: Generate global statements (including `var G = 0`)

**The Problem:**
- When function `print()` is being compiled (step 1), it tries to look up `G` in `gen->symbols`
- But `G` hasn't been added yet - it's added in step 2!
- Result: "Undefined variable: G"

### Code Evidence

**codegen.c line 1723-1730:**
```c
// Generate non-function statements in main
if (ast->type == AST_PROGRAM || ast->type == AST_BLOCK) {
    for (int i = 0; i < ast->program.count; i++) {
        ASTNode* stmt = ast->program.statements[i];
        
        // Skip function declarations (already handled)
        if (stmt->type == AST_FUNCTION_DECL) {
            continue;
        }
        
        // Generate the statement normally  <- Global vars added here!
        codegen_node(gen, stmt);
```

This runs AFTER function bodies are compiled, so global variables aren't available when compiling functions.

## The Fix

We need to generate global variables BEFORE compiling function bodies.

**Option 1: Two-pass approach**
1. First pass: Generate all global variable declarations (add to gen->symbols)
2. Second pass: Generate function bodies (can now access globals)
3. Third pass: Generate remaining statements (initializations, function calls)

**Option 2: Hoist global variables**
Before generating functions, scan the AST for AST_VAR_DECL nodes and generate them first.

**Recommended: Option 1 (cleaner separation)**

### Implementation Plan
1. Add a new pass before line 1615 to collect and generate global variables
2. Generate only the alloca + initial value, add to symbol table
3. Then generate functions (which can now see globals)
4. Finally generate remaining statements (assignments, calls, etc.)

## Test Case
After the fix, `examples/globals/simple.js` should:
- Compile without errors
- Output: `0`
