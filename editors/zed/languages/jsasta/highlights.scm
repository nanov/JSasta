; Keywords
[
  "var"
  "let"
  "const"
  "function"
  "external"
  "struct"
  "return"
  "if"
  "else"
  "for"
  "while"
] @keyword

; Types
[
  "int"
  "string"
  "bool"
  "void"
] @type.builtin

; Literals
(number) @number
(string) @string
(true) @constant.builtin.boolean
(false) @constant.builtin.boolean

; Comments
(comment) @comment

; Functions
(function_declaration
  name: (identifier) @function)

(external_declaration
  name: (identifier) @function)

(call_expression
  function: (identifier) @function.call)

; Structs
(struct_declaration
  name: (identifier) @type)

(struct_field
  name: (identifier) @property)

; Variables
(variable_declaration
  name: (identifier) @variable)

(parameter
  name: (identifier) @variable.parameter)

; Properties
(member_expression
  property: (identifier) @property)

(pair
  key: (identifier) @property)

; Operators
[
  "="
  "+"
  "-"
  "*"
  "/"
  "%"
  "=="
  "!="
  "<"
  "<="
  ">"
  ">="
  "&&"
  "||"
  "!"
  "++"
  "--"
  "+="
  "-="
  "*="
  "/="
  "<<"
  ">>"
  "&"
] @operator

; Punctuation
[
  "("
  ")"
  "{"
  "}"
  "["
  "]"
] @punctuation.bracket

[
  ";"
  ","
  "."
  ":"
] @punctuation.delimiter

"?" @punctuation.special
"..." @punctuation.special
