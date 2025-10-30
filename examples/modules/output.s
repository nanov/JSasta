	.build_version macos, 15, 0
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_main                           ; -- Begin function main
	.p2align	2
_main:                                  ; @main
	.cfi_startproc
; %bb.0:                                ; %entry
	sub	sp, sp, #32
	stp	x29, x30, [sp, #16]             ; 16-byte Folded Spill
	.cfi_def_cfa_offset 32
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	mov	w0, #5                          ; =0x5
	mov	w1, #3                          ; =0x3
	bl	_add_i32_i32
	mov	w8, w0
	str	w0, [sp, #8]
Lloh0:
	adrp	x0, l_str@PAGE
Lloh1:
	add	x0, x0, l_str@PAGEOFF
	str	x8, [sp]
	bl	_printf
	mov	w0, #4                          ; =0x4
	mov	w1, #7                          ; =0x7
	bl	_multiply_i32_i32
	mov	w8, w0
	str	w0, [sp, #12]
Lloh2:
	adrp	x0, l_str.2@PAGE
Lloh3:
	add	x0, x0, l_str.2@PAGEOFF
	str	x8, [sp]
	bl	_printf
	ldp	x29, x30, [sp, #16]             ; 16-byte Folded Reload
	add	sp, sp, #32
	ret
	.loh AdrpAdd	Lloh2, Lloh3
	.loh AdrpAdd	Lloh0, Lloh1
	.cfi_endproc
                                        ; -- End function
	.globl	_multiply_i32_i32               ; -- Begin function multiply_i32_i32
	.p2align	2
_multiply_i32_i32:                      ; @multiply_i32_i32
	.cfi_startproc
; %bb.0:                                ; %entry
	sub	sp, sp, #16
	.cfi_def_cfa_offset 16
	mul	w8, w0, w1
	stp	w0, w1, [sp, #8]
	mov	w0, w8
	add	sp, sp, #16
	ret
	.cfi_endproc
                                        ; -- End function
	.globl	_add_i32_i32                    ; -- Begin function add_i32_i32
	.p2align	2
_add_i32_i32:                           ; @add_i32_i32
	.cfi_startproc
; %bb.0:                                ; %entry
	sub	sp, sp, #16
	.cfi_def_cfa_offset 16
	stp	w0, w1, [sp, #8]
	add	w0, w0, w1
	add	sp, sp, #16
	ret
	.cfi_endproc
                                        ; -- End function
	.globl	_main.7                         ; -- Begin function main.7
	.p2align	2
_main.7:                                ; @main.7
	.cfi_startproc
; %bb.0:                                ; %entry
	mov	w0, wzr
	ret
	.cfi_endproc
                                        ; -- End function
	.section	__TEXT,__cstring,cstring_literals
l_str:                                  ; @str
	.asciz	"5 + 3 = %d\n"

l_str.2:                                ; @str.2
	.asciz	"4 * 7 = %d\n"

l_str.8:                                ; @str.8
	.asciz	"5 + 3 = %d\n"

l_str.9:                                ; @str.9
	.asciz	"4 * 7 = %d\n"

.subsections_via_symbols
