/* PGM opaque background spans, JZ4740 MXU Release-1. GPL-2.0-or-later.
 * a0=RGB32 destination, a1=first indexed source pixel, a2=32-color palette,
 * a3=width (0..32), stack+16=rows (0..32), stack+20=flags.
 * flags: bit0 source X decrements, bit1 source rows decrement by 32 bytes.
 * The destination row pitch is 448 words. Source/palette remain unchanged.
 * Four independent S32LDDV gathers precede four S32STD stores. There is no
 * transparency branch: callers supply only rows proven wholly opaque.
 * XR1..XR4 and MXU_CR are preserved. CP0/interrupt state is never touched.
 */
.set mips32
.set noreorder
.set noat
.macro bg_m2i xr, gr
 .word (0x7000002e | (\gr << 16) | (\xr << 6))
.endm
.macro bg_i2m xr, gr
 .word (0x7000002f | (\gr << 16) | (\xr << 6))
.endm
.macro bg_gather xr, gr
 .word (0x70000012 | (6 << 21) | (\gr << 16) | (2 << 14) | (\xr << 6))
.endm
.macro bg_store xr, offset
 .word (0x70000011 | (4 << 21) | (((\offset / 4) & 1023) << 10) | (\xr << 6))
.endm
.macro bg_rows direction, label
\label\()_row:
 move $a3,$t4
 move $a1,$t8
 sltiu $t0,$a3,4
 bnez $t0,\label\()_tail
 nop
\label\()_four:
 lbu $t0,0($a1)
 lbu $t1,(\direction)($a1)
 lbu $t2,(2*\direction)($a1)
 lbu $t3,(3*\direction)($a1)
 bg_gather 1,8
 bg_gather 2,9
 bg_gather 3,10
 bg_gather 4,11
 bg_store 1,0
 bg_store 2,4
 bg_store 3,8
 bg_store 4,12
 addiu $a3,$a3,-4
 addiu $a1,$a1,(4*\direction)
 sltiu $t0,$a3,4
 beqz $t0,\label\()_four
 addiu $a0,$a0,16
 beqz $a3,\label\()_next
 nop
\label\()_tail:
 lbu $t0,0($a1)
 addiu $a1,$a1,\direction
 bg_gather 1,8
 addiu $a3,$a3,-1
 bg_store 1,0
 bnez $a3,\label\()_tail
 addiu $a0,$a0,4
\label\()_next:
 addiu $t5,$t5,-1
 addu $t8,$t8,$t6
 bnez $t5,\label\()_row
 addu $a0,$a0,$t9
 b .Lbg_done
 nop
.endm
.section .text.h1_pgm_bg_mxu_span,"ax",@progbits
.globl h1_pgm_bg_mxu_span
.type h1_pgm_bg_mxu_span,@function
h1_pgm_bg_mxu_span:
 lw $t5,16($sp)
 lw $t7,20($sp)
 beqz $a3,.Lbg_return
 move $t4,$a3
 beqz $t5,.Lbg_return
 nop
 addiu $sp,$sp,-24
 bg_m2i 16,8
 sw $t0,16($sp)
 ori $t0,$t0,1
 bg_i2m 16,8
 nop
 nop
 nop
 bg_m2i 1,8
 sw $t0,0($sp)
 bg_m2i 2,8
 sw $t0,4($sp)
 bg_m2i 3,8
 sw $t0,8($sp)
 bg_m2i 4,8
 sw $t0,12($sp)
 move $t8,$a1
 li $t6,32
 andi $t0,$t7,2
 beqz $t0,.Lbg_step_ready
 nop
 li $t6,-32
.Lbg_step_ready:
 sll $t0,$t4,2
 li $t9,1792
 subu $t9,$t9,$t0
 andi $t7,$t7,1
 beqz $t7,.Lbg_forward_row
 nop
 bg_rows -1,.Lbg_reverse
 bg_rows 1,.Lbg_forward
.Lbg_done:
 lw $t0,0($sp)
 bg_i2m 1,8
 lw $t0,4($sp)
 bg_i2m 2,8
 lw $t0,8($sp)
 bg_i2m 3,8
 lw $t0,12($sp)
 bg_i2m 4,8
 lw $t0,16($sp)
 bg_i2m 16,8
 nop
 nop
 nop
 addiu $sp,$sp,24
.Lbg_return:
 jr $ra
 nop
.size h1_pgm_bg_mxu_span,.-h1_pgm_bg_mxu_span
.set at
.set reorder
