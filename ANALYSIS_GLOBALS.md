# Analysis: Global Variable Access from Functions

## Current Situation

### Test File: `examples/globals/simple.js`
```javascript
var G = 0;

function print():void {
    console.log(G);  // Error: Undefined variable: G
}

print();
```

### Compilation Result
```
[ERROR]   Undefined variable: G
[ERROR]   Undefined variable: G
```

## Problem Analysis

The global variable `G` is defined at the top level but cannot be accessed from within the `print()` function. This suggests:

1. **Scope Issue**: The symbol table used during function body compilation doesn't include global variables
2. **Lookup Issue**: When resolving identifiers inside functions, the compiler only looks in the local function scope
3. **Two Errors**: Both errors likely occur at:
   - Type inference phase (checking the type of G)
   - Code generation phase (looking up G's value)

## Expected Behavior

Global variables should be accessible from any function. The expected output should be:
```
0
```

## Root Cause Hypothesis

When parsing/analyzing function bodies, the compiler likely:
1. Creates a new symbol table scope for the function
2. Does NOT link this scope to the parent/global scope
3. Therefore, lookups for `G` fail because it's not in the function's local scope

## What Needs Investigation

1. **Symbol Table Structure**: 
   - How are scopes created for functions?
   - Is there a parent scope chain?
   
2. **Type Inference**:
   - Where does it look up variables in function bodies?
   - Does it check parent scopes?

3. **Code Generation**:
   - How are variables looked up during codegen?
   - Are global variables stored differently than local ones?

## Next Steps

1. Find where function scopes are created
2. Find where variable lookups happen in function bodies
3. Ensure lookups check parent/global scope
4. Test the fix with this example

## Related Code Areas to Examine

- `src/symbol_table.c` - Symbol table implementation
- `src/type_inference.c` - Variable lookup during type inference
- `src/codegen.c` - Variable lookup during code generation
- `src/parser.c` - How function scopes are created

## Deep Dive Analysis Results

### Symbol Table Structure ✅
- Symbol tables support parent scopes via `table->parent`
- `symbol_table_lookup` correctly checks parent scopes recursively
- Function scopes are created with `symbol_table_create(parent_scope)`

### Function Scope Creation ✅
In type_inference.c line 1224:
```c
SymbolTable* func_scope = symbol_table_create(symbols);  // Linked to parent!
```

In codegen.c lines 1285, 1393:
```c
SymbolTable* func_scope = symbol_table_create(gen->symbols);  // Linked to parent!
```

### Variable Lookup ✅
Line 85-86 in infer_expr_type_simple:
```c
SymbolEntry* entry = symbol_table_lookup(scope, node->identifier.name);
```
This correctly uses symbol_table_lookup which checks parent scopes.

## ROOT CAUSE FOUND ❌

### The Problem
**Global variables are NEVER added to the symbol table during type inference!**

Looking at `type_inference_with_context` (line 1587):
1. Pass 0: Collect struct declarations
2. Pass 1: Collect function signatures  
3. Pass 2: Infer literal types (AST_VAR_DECL case at line 416)
4. Pass 3-5: Iterative specialization

**In Pass 2 (infer_literal_types), the AST_VAR_DECL case:**
```c
case AST_VAR_DECL:
    if (node->var_decl.init) {
        infer_literal_types(node->var_decl.init, symbols, type_ctx);
        // NO symbol_table_insert HERE!
    }
```

Global variables are only added to the symbol table inside functions:
- Line 183: Inside `infer_function_return_type_with_params` 
- But this only handles local variables inside functions!

### The Fix
We need to add global variables to the symbol table during one of the early passes, so they're available when analyzing function bodies.

**Option 1:** Add to symbol table in Pass 2 (infer_literal_types) when processing AST_VAR_DECL
**Option 2:** Add a new Pass 1.5 specifically for collecting global variables  
**Option 3:** Modify Pass 1 to collect both functions AND global variables

**Recommended:** Option 1 - modify the AST_VAR_DECL case in infer_literal_types (line 416)
