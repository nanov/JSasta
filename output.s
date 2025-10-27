	.build_version macos, 15, 0
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_main                           ; -- Begin function main
	.p2align	2
_main:                                  ; @main
	.cfi_startproc
; %bb.0:                                ; %entry
	sub	sp, sp, #96
	stp	x22, x21, [sp, #48]             ; 16-byte Folded Spill
	stp	x20, x19, [sp, #64]             ; 16-byte Folded Spill
	stp	x29, x30, [sp, #80]             ; 16-byte Folded Spill
	.cfi_def_cfa_offset 96
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	.cfi_offset w19, -24
	.cfi_offset w20, -32
	.cfi_offset w21, -40
	.cfi_offset w22, -48
	mov	x8, #3                          ; =0x3
	mov	x9, #4609434218613702656        ; =0x3ff8000000000000
	mov	w0, #32                         ; =0x20
	movk	x8, #4, lsl #32
	stp	x8, xzr, [sp, #32]
	mov	x8, #4612811918334230528        ; =0x4004000000000000
	stp	x9, x8, [sp, #16]
	bl	_malloc
Lloh0:
	adrp	x1, l_int_fmt@PAGE
Lloh1:
	add	x1, x1, l_int_fmt@PAGEOFF
	mov	x19, x0
	str	xzr, [sp]
	bl	_sprintf
Lloh2:
	adrp	x20, l_str@PAGE
Lloh3:
	add	x20, x20, l_str@PAGEOFF
	mov	x0, x20
	bl	_strlen
	mov	x21, x0
	mov	x0, x19
	bl	_strlen
	add	x8, x21, x0
	add	x0, x8, #1
	bl	_malloc
	mov	x1, x20
	mov	x21, x0
	bl	_strcpy
	mov	x0, x21
	mov	x1, x19
	bl	_strcat
Lloh4:
	adrp	x0, l_fmt@PAGE
Lloh5:
	add	x0, x0, l_fmt@PAGEOFF
	str	x21, [sp]
	bl	_printf
Lloh6:
	adrp	x0, l_newline@PAGE
Lloh7:
	add	x0, x0, l_newline@PAGEOFF
	bl	_printf
	ldr	w20, [sp, #36]
	mov	w0, #32                         ; =0x20
	bl	_malloc
Lloh8:
	adrp	x1, l_int_fmt.2@PAGE
Lloh9:
	add	x1, x1, l_int_fmt.2@PAGEOFF
	mov	x19, x0
	str	x20, [sp]
	bl	_sprintf
Lloh10:
	adrp	x20, l_str.1@PAGE
Lloh11:
	add	x20, x20, l_str.1@PAGEOFF
	mov	x0, x20
	bl	_strlen
	mov	x21, x0
	mov	x0, x19
	bl	_strlen
	add	x8, x21, x0
	add	x0, x8, #1
	bl	_malloc
	mov	x1, x20
	mov	x21, x0
	bl	_strcpy
	mov	x0, x21
	mov	x1, x19
	bl	_strcat
Lloh12:
	adrp	x0, l_fmt.3@PAGE
Lloh13:
	add	x0, x0, l_fmt.3@PAGEOFF
	str	x21, [sp]
	bl	_printf
Lloh14:
	adrp	x0, l_newline.4@PAGE
Lloh15:
	add	x0, x0, l_newline.4@PAGEOFF
	bl	_printf
	ldr	w20, [sp, #16]
	mov	w0, #64                         ; =0x40
	bl	_malloc
Lloh16:
	adrp	x1, l_double_fmt@PAGE
Lloh17:
	add	x1, x1, l_double_fmt@PAGEOFF
	mov	x19, x0
	str	x20, [sp]
	bl	_sprintf
Lloh18:
	adrp	x20, l_str.5@PAGE
Lloh19:
	add	x20, x20, l_str.5@PAGEOFF
	mov	x0, x20
	bl	_strlen
	mov	x21, x0
	mov	x0, x19
	bl	_strlen
	add	x8, x21, x0
	add	x0, x8, #1
	bl	_malloc
	mov	x1, x20
	mov	x21, x0
	bl	_strcpy
	mov	x0, x21
	mov	x1, x19
	bl	_strcat
Lloh20:
	adrp	x0, l_fmt.6@PAGE
Lloh21:
	add	x0, x0, l_fmt.6@PAGEOFF
	str	x21, [sp]
	bl	_printf
Lloh22:
	adrp	x0, l_newline.7@PAGE
Lloh23:
	add	x0, x0, l_newline.7@PAGEOFF
	bl	_printf
	ldp	x29, x30, [sp, #80]             ; 16-byte Folded Reload
	mov	w0, wzr
	ldp	x20, x19, [sp, #64]             ; 16-byte Folded Reload
	ldp	x22, x21, [sp, #48]             ; 16-byte Folded Reload
	add	sp, sp, #96
	ret
	.loh AdrpAdd	Lloh22, Lloh23
	.loh AdrpAdd	Lloh20, Lloh21
	.loh AdrpAdd	Lloh18, Lloh19
	.loh AdrpAdd	Lloh16, Lloh17
	.loh AdrpAdd	Lloh14, Lloh15
	.loh AdrpAdd	Lloh12, Lloh13
	.loh AdrpAdd	Lloh10, Lloh11
	.loh AdrpAdd	Lloh8, Lloh9
	.loh AdrpAdd	Lloh6, Lloh7
	.loh AdrpAdd	Lloh4, Lloh5
	.loh AdrpAdd	Lloh2, Lloh3
	.loh AdrpAdd	Lloh0, Lloh1
	.cfi_endproc
                                        ; -- End function
	.section	__TEXT,__cstring,cstring_literals
l_str:                                  ; @str
	.asciz	"p1.x: "

l_int_fmt:                              ; @int_fmt
	.asciz	"%d"

l_fmt:                                  ; @fmt
	.asciz	"%s"

l_newline:                              ; @newline
	.asciz	"\n"

l_str.1:                                ; @str.1
	.asciz	"p2.y: "

l_int_fmt.2:                            ; @int_fmt.2
	.asciz	"%d"

l_fmt.3:                                ; @fmt.3
	.asciz	"%s"

l_newline.4:                            ; @newline.4
	.asciz	"\n"

l_str.5:                                ; @str.5
	.asciz	"p3.x: "

l_double_fmt:                           ; @double_fmt
	.asciz	"%f"

l_fmt.6:                                ; @fmt.6
	.asciz	"%s"

l_newline.7:                            ; @newline.7
	.asciz	"\n"

.subsections_via_symbols
