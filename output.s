	.build_version macos, 15, 0
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_multiply_double_double         ; -- Begin function multiply_double_double
	.p2align	2
_multiply_double_double:                ; @multiply_double_double
	.cfi_startproc
; %bb.0:                                ; %entry
	sub	sp, sp, #16
	.cfi_def_cfa_offset 16
	fmul	d2, d0, d1
	stp	d1, d0, [sp], #16
	fmov	d0, d2
	ret
	.cfi_endproc
                                        ; -- End function
	.globl	_multiply_int_int               ; -- Begin function multiply_int_int
	.p2align	2
_multiply_int_int:                      ; @multiply_int_int
	.cfi_startproc
; %bb.0:                                ; %entry
	sub	sp, sp, #16
	.cfi_def_cfa_offset 16
	mul	w8, w0, w1
	stp	w1, w0, [sp, #8]
	mov	w0, w8
	add	sp, sp, #16
	ret
	.cfi_endproc
                                        ; -- End function
	.globl	_add                            ; -- Begin function add
	.p2align	2
_add:                                   ; @add
	.cfi_startproc
; %bb.0:                                ; %entry
	sub	sp, sp, #16
	.cfi_def_cfa_offset 16
	stp	w1, w0, [sp, #8]
	add	w0, w0, w1
	add	sp, sp, #16
	ret
	.cfi_endproc
                                        ; -- End function
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
	bl	_add
	mov	w8, w0
Lloh0:
	adrp	x0, l_str@PAGE
Lloh1:
	add	x0, x0, l_str@PAGEOFF
	str	x8, [sp]
	bl	_printf
	mov	w0, #4                          ; =0x4
	mov	w1, #6                          ; =0x6
	bl	_multiply_int_int
	mov	w8, w0
Lloh2:
	adrp	x0, l_str.2@PAGE
Lloh3:
	add	x0, x0, l_str.2@PAGEOFF
	str	x8, [sp]
	bl	_printf
	fmov	d0, #2.50000000
	fmov	d1, #3.00000000
	bl	_multiply_double_double
Lloh4:
	adrp	x0, l_str.3@PAGE
Lloh5:
	add	x0, x0, l_str.3@PAGEOFF
	str	d0, [sp]
	bl	_printf
Lloh6:
	adrp	x0, l_str.4@PAGE
Lloh7:
	add	x0, x0, l_str.4@PAGEOFF
	bl	_printf
	ldp	x29, x30, [sp, #16]             ; 16-byte Folded Reload
	mov	w0, wzr
	add	sp, sp, #32
	ret
	.loh AdrpAdd	Lloh6, Lloh7
	.loh AdrpAdd	Lloh4, Lloh5
	.loh AdrpAdd	Lloh2, Lloh3
	.loh AdrpAdd	Lloh0, Lloh1
	.cfi_endproc
                                        ; -- End function
	.section	__TEXT,__cstring,cstring_literals
l_str:                                  ; @str
	.asciz	"User function: 5 + 3 = %d\n"

l_str.2:                                ; @str.2
	.asciz	"Specialized (int): 4 * 6 = %d\n"

l_str.3:                                ; @str.3
	.asciz	"Specialized (double): 2.5 * 3.0 = %f\n"

l_str.4:                                ; @str.4
	.asciz	"All tests passed!\n"

.subsections_via_symbols
