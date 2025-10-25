" Vim syntax file
" Language: JSasta
" Maintainer: JSasta Compiler Team
" Latest Revision: 2025

if exists("b:current_syntax")
  finish
endif

" Keywords
syn keyword jeastaKeyword if else while for return break continue
syn keyword jeastaDeclaration var let const function
syn keyword jeastaBoolean true false
syn keyword jeastaNull null undefined

" Types
syn keyword jeastaType int double string bool void

" Comments
syn match jeastaComment "\/\/.*$"
syn region jeastaComment start="/\*" end="\*/"

" Strings
syn region jeastaString start='"' end='"' skip='\\"' contains=jeastaEscape
syn region jeastaString start="'" end="'" skip="\\'" contains=jeastaEscape
syn match jeastaEscape "\\\([\\"'nrt]\|x[0-9a-fA-F]\{2}\)" contained

" Numbers
syn match jeastaNumber "\<\d\+\>"
syn match jeastaFloat "\<\d\+\.\d*\>"
syn match jeastaFloat "\<\.\d\+\>"

" Functions
syn match jeastaFunction "\<function\s\+\w\+"
syn match jeastaFunctionCall "\<\w\+\s*("he=e-1

" Type annotations
syn match jeastaTypeAnnotation ":\s*\(int\|double\|string\|bool\|void\)"
syn region jeastaObjectType start=":\s*{" end="}" contains=jeastaTypeAnnotation,jeastaPropertyType
syn match jeastaPropertyType "\w\+\s*:\s*\(int\|double\|string\|bool\)" contained

" Operators
syn match jeastaOperator "\(+\|-\|\*\|/\|%\|==\|!=\|<\|>\|<=\|>=\|&&\|||\|!\|=\|+=\|-=\|\*=\|/=\|++\|--\|<<\|>>\|&\)"

" Special characters
syn match jeastaSpecial "[{}()\[\].,;:]"

" Member access
syn match jeastaMemberAccess "\.\w\+"

" Highlighting
hi def link jeastaKeyword Keyword
hi def link jeastaDeclaration Keyword
hi def link jeastaBoolean Boolean
hi def link jeastaNull Constant
hi def link jeastaType Type
hi def link jeastaComment Comment
hi def link jeastaString String
hi def link jeastaEscape SpecialChar
hi def link jeastaNumber Number
hi def link jeastaFloat Float
hi def link jeastaFunction Function
hi def link jeastaFunctionCall Function
hi def link jeastaTypeAnnotation Type
hi def link jeastaObjectType Type
hi def link jeastaPropertyType Type
hi def link jeastaOperator Operator
hi def link jeastaSpecial Special
hi def link jeastaMemberAccess Identifier

let b:current_syntax = "jsasta"
