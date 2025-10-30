# JSasta Error Diagnostic Examples

This directory contains examples demonstrating all error codes in the JSasta compiler. Each file is designed to trigger a specific error to help understand the diagnostic system.

## Overview

JSasta uses a Rust-inspired diagnostic system with error codes organized by compilation phase:

- **E2xx**: Parser errors (syntax and structural issues)
- **T3xx**: Type inference errors (type checking and semantic issues)

## Parser Errors (E201-E229)

### Object Literal Errors

| Code | File | Description |
|------|------|-------------|
| E201 | `e201_object_property_name.jsa` | Expected property name in object literal |

### Expression Errors

| Code | File | Description |
|------|------|-------------|
| E202 | `e202_unexpected_token.jsa` | Unexpected token in expression |
| E203 | `e203_postfix_operator.jsa` | Postfix operator can only be applied to lvalues |
| E204 | `e204_member_access_identifier.jsa` | Expected identifier after '.' |
| E205 | `e205_property_name.jsa` | Expected property name after '.' (prefix context) |
| E206 | `e206_missing_bracket.jsa` | Expected ']' after index expression |
| E207 | `e207_prefix_operand.jsa` | Expected operand after prefix operator |

### Assignment Errors

| Code | File | Description |
|------|------|-------------|
| E208 | `e208_invalid_assignment.jsa` | Invalid assignment target (not an lvalue) |
| E209 | `e209_compound_assignment.jsa` | Compound assignment requires lvalue |

### Variable Declaration Errors

| Code | File | Description |
|------|------|-------------|
| E210 | `e210_type_keyword_as_variable.jsa` | Cannot use type keyword as variable name |
| E211 | `e211_missing_identifier.jsa` | Expected identifier after var/let/const |
| E212 | `e212_array_size_missing.jsa` | Expected array size expression after '[' |
| E213 | `e213_array_bracket.jsa` | Expected ']' after array size |
| E214 | `e214_missing_initializer.jsa` | Expected expression after = |

### External Function Errors

| Code | File | Description |
|------|------|-------------|
| E215 | `e215_external_param_type.jsa` | External function parameters must have type annotations |
| E216 | `e216_unknown_type.jsa` | Unknown type in external function parameter |
| E217 | `e217_external_param_name.jsa` | Expected parameter name or type in external declaration |
| E218 | `e218_external_return_type.jsa` | External function must have return type annotation |

### Struct Declaration Errors

| Code | File | Description |
|------|------|-------------|
| E219 | `e219_struct_name.jsa` | Expected struct name after 'struct' keyword |
| E220 | `e220_method_param_name.jsa` | Expected parameter name in method |
| E221 | `e221_method_param_type.jsa` | Method parameter must have type annotation |
| E222 | `e222_method_return_type.jsa` | Method must have return type annotation |
| E223 | `e223_struct_member_name.jsa` | Expected property or method name in struct |
| E224 | `e224_struct_property_type.jsa` | Struct property must have type annotation |
| E225 | `e225_struct_array_size.jsa` | Expected array size expression in struct field |
| E226 | `e226_struct_array_bracket.jsa` | Expected ']' after array size in struct field |
| E227 | `e227_array_explicit_size.jsa` | Array fields must have explicit size (not i32[]) |
| E228 | `e228_default_value_literal.jsa` | Default values must be literals |

### Parser Recovery Errors

| Code | File | Description |
|------|------|-------------|
| E229 | `e229_stuck_token.jsa` | Parser stuck on token (recovery failed) |

## Type Inference Errors (T301-T315)

### Variable and Identifier Errors

| Code | File | Description |
|------|------|-------------|
| T301 | `t301_undefined_variable.jsa` | Undefined variable reference |

### Method Call Errors

| Code | File | Description |
|------|------|-------------|
| T302 | `t302_method_not_found.jsa` | Method not found on struct |
| T302 | `t302_method_on_primitive.jsa` | Cannot call method on primitive type |

### Trait Implementation Errors

| Code | File | Description |
|------|------|-------------|
| T304 | `t304_index_trait.jsa` | Type does not implement Index<T> |
| T304 | `t304_index_wrong_type.jsa` | Type does not implement Index<T> with given type |
| T305 | `t305_refindex_trait.jsa` | Type does not implement RefIndex<T> (for assignment) |

### Struct Type Errors

| Code | File | Description |
|------|------|-------------|
| T306 | `t306_struct_default_type_mismatch.jsa` | Struct default value type mismatch |
| T307 | `t307_variable_type_mismatch.jsa` | Variable declaration type mismatch |
| T308 | `t308_property_type_mismatch.jsa` | Property type mismatch in struct literal |
| T309 | `t309_unknown_property.jsa` | Unknown property in struct literal |
| T310 | `t310_missing_required_property.jsa` | Missing required property in struct |
| T311 | `t311_member_assignment_type_mismatch.jsa` | Type mismatch in member assignment |

### Function Call Errors

| Code | File | Description |
|------|------|-------------|
| T312 | `t312_function_param_type_mismatch.jsa` | Function parameter type mismatch |

### Array Size and Const Errors

| Code | File | Description |
|------|------|-------------|
| T313 | `t313_invalid_array_size.jsa` | Invalid array size (negative) |
| T313 | `t313_array_size_not_const.jsa` | Array size must be const |
| T313 | `t313_array_size_float.jsa` | Array size must be integer |
| T314 | `t314_const_eval_error.jsa` | Const expression evaluation error |
| T314 | `t314_const_eval_negative.jsa` | Const expression result not positive |
| T315 | `t315_circular_dependency.jsa` | Circular dependency in const |
| T315 | `t315_undefined_const_reference.jsa` | Undefined reference in const |

## File Naming Convention

Each file follows the pattern: `<code>_<description>.jsa`

Examples:
- `e201_object_property_name.jsa`
- `t301_undefined_variable.jsa`
- `t309_unknown_property.jsa`

## Testing the Examples

You can test any example by compiling it:

```bash
./build/jsastac examples/error-diagnostic/t301_undefined_variable.jsa
```

Expected output:
```
[ERROR:T301] examples/error-diagnostic/t301_undefined_variable.jsa:6:18: Undefined variable: undefinedVar
```

## Diagnostic Features

The JSasta diagnostic system includes:

1. **Error Codes**: Unique identifiers for each error type (E2xx, T3xx)
2. **Source Locations**: Precise filename, line, and column information
3. **Colored Output**: ANSI colors for better readability (when terminal supports it)
4. **Severity Levels**: ERROR, WARNING, INFO, HINT
5. **Logger Integration**: Respects quiet/verbose modes
6. **Multi-pass Collection**: Reports as many errors as possible in one compilation

## Diagnostic System Architecture

### Collection Modes

- **COLLECT Mode**: Collects all diagnostics during compilation, reports at the end
- **DIRECT Mode**: Emits diagnostics immediately as they occur

### Error Collection Strategy

The compiler continues through all type checking passes to report maximum errors:

1. **Pass 0**: Collect consts and structs
2. **Pass 1**: Collect function signatures  
3. **Pass 2-4**: Iterative specialization discovery
4. **Stop Point**: Only stops before codegen if errors are found

This Rust-inspired approach minimizes "fix one, compile again" cycles.

## Future Enhancements

Potential improvements to make the system more Rust-like:

- [ ] **Spans**: Full start/end positions instead of single locations
- [ ] **Suggestions**: "try this instead" with code snippets
- [ ] **Error Explanations**: `jsastac --explain T301` for detailed docs
- [ ] **Code Snippets**: Show actual source with carets pointing to errors
- [ ] **Notes/Hints**: Additional context for complex errors

## Error Code Naming Convention

- **E2xx**: Parser phase (E = Error, 2 = parsing phase)
  - E201-E210: Expression and literal errors
  - E211-E218: Declaration and external function errors
  - E219-E228: Struct declaration errors
  - E229: Parser recovery failures

- **T3xx**: Type inference phase (T = Type, 3 = type checking phase)
  - T301: Variable resolution
  - T302: Method calls
  - T304-T305: Trait implementations
  - T306-T311: Type mismatches
  - T312: Function calls
  - T313-T315: Const evaluation and array sizes

## Contributing

When adding new error codes:

1. Choose the appropriate range (E2xx for parser, T3xx for type inference)
2. Create an example file following the naming convention
3. Update this README with the new error code
4. Use the diagnostic macros (PARSE_ERROR, TYPE_ERROR) consistently
