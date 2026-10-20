/* PGM indexed RGB32 row composition, JZ4740 MXU Release-1.
 * GPL-2.0-or-later.
 *
 * The three bounded leaves preserve XR1 and MXU_CR in caller-saved GPRs.
 * Only XR1 is touched; CP0 Status and interrupt enable are never modified.
 * Palette entries 0..31 are opaque and index 255 is transparent. A zero
 * count returns before enabling MXU. dst/palette must be word aligned and
 * map columns halfword aligned. No destination reads or speculative source
 * reads are used. The return value is the number of opaque stores.
 */
.set mips32
.set noreorder
.set noat

.macro pgm_mxu_m2i xr, gr
 .word (0x7000002e | (\gr << 16) | (\xr << 6))
.endm
.macro pgm_mxu_i2m xr, gr
 .word (0x7000002f | (\gr << 16) | (\xr << 6))
.endm
.macro pgm_mxu_begin
 pgm_mxu_m2i 16,14 /* t6 = original MXU_CR */
 ori $t5,$t6,1
 pgm_mxu_i2m 16,13
 nop
 nop
 nop
 pgm_mxu_m2i 1,15 /* t7 = original XR1 */
 li $t9,255
.endm
.macro pgm_mxu_end
 pgm_mxu_i2m 1,15
 pgm_mxu_i2m 16,14
 nop
 nop
 nop
.endm

/* Branch delay slots consume a source element even when transparent. The
 * palette gather uses the unmodified 8-bit index with a word scale of 2.
 * Incrementing the opaque count separates S32LDDV from its dependent store.
 */
.macro pgm_mxu_store index, gr, offset, count, skip
 beq \index,$t9,\skip
 addiu \count,\count,-1
 .word (0x70000012 | (6 << 21) | (\gr << 16) | (2 << 14) | (1 << 6))
 addiu $v0,$v0,1
 .word (0x70000011 | (4 << 21) | (((\offset / 4) & 1023) << 10) | (1 << 6))
\skip:
.endm

/* a0=dst, a1=contiguous source bytes, a2=palette, a3=count. */
.section .text.h1_pgm_sprite_mxu_row,"ax",@progbits
.globl h1_pgm_sprite_mxu_row
.type h1_pgm_sprite_mxu_row,@function
h1_pgm_sprite_mxu_row:
 beqz $a3,.Lmxrow_return
 move $v0,$zero
 pgm_mxu_begin
 sltiu $t4,$a3,4
 bnez $t4,.Lmxrow_tail
 nop
.Lmxrow_four:
 lbu $t0,0($a1)
 lbu $t1,1($a1)
 lbu $t2,2($a1)
 lbu $t3,3($a1)
 pgm_mxu_store $t0,8,0,$a3,.Lmxrow_skip0
 pgm_mxu_store $t1,9,4,$a3,.Lmxrow_skip1
 pgm_mxu_store $t2,10,8,$a3,.Lmxrow_skip2
 pgm_mxu_store $t3,11,12,$a3,.Lmxrow_skip3
 addiu $a1,$a1,4
 sltiu $t4,$a3,4
 beqz $t4,.Lmxrow_four
 addiu $a0,$a0,16
 beqz $a3,.Lmxrow_done
 nop
.Lmxrow_tail:
 lbu $t0,0($a1)
 addiu $a1,$a1,1
 pgm_mxu_store $t0,8,0,$a3,.Lmxrow_tail_skip
 bnez $a3,.Lmxrow_tail
 addiu $a0,$a0,4
.Lmxrow_done:
 pgm_mxu_end
.Lmxrow_return:
 jr $ra
 nop
.size h1_pgm_sprite_mxu_row,.-h1_pgm_sprite_mxu_row

/* a0=dst, a1=rightmost source byte, a2=palette, a3=count. */
.section .text.h1_pgm_sprite_mxu_rev,"ax",@progbits
.globl h1_pgm_sprite_mxu_rev
.type h1_pgm_sprite_mxu_rev,@function
h1_pgm_sprite_mxu_rev:
 beqz $a3,.Lmxrev_return
 move $v0,$zero
 pgm_mxu_begin
 sltiu $t4,$a3,4
 bnez $t4,.Lmxrev_tail
 nop
.Lmxrev_four:
 lbu $t0,0($a1)
 lbu $t1,-1($a1)
 lbu $t2,-2($a1)
 lbu $t3,-3($a1)
 pgm_mxu_store $t0,8,0,$a3,.Lmxrev_skip0
 pgm_mxu_store $t1,9,4,$a3,.Lmxrev_skip1
 pgm_mxu_store $t2,10,8,$a3,.Lmxrev_skip2
 pgm_mxu_store $t3,11,12,$a3,.Lmxrev_skip3
 addiu $a1,$a1,-4
 sltiu $t4,$a3,4
 beqz $t4,.Lmxrev_four
 addiu $a0,$a0,16
 beqz $a3,.Lmxrev_done
 nop
.Lmxrev_tail:
 lbu $t0,0($a1)
 addiu $a1,$a1,-1
 pgm_mxu_store $t0,8,0,$a3,.Lmxrev_tail_skip
 bnez $a3,.Lmxrev_tail
 addiu $a0,$a0,4
.Lmxrev_done:
 pgm_mxu_end
.Lmxrev_return:
 jr $ra
 nop
.size h1_pgm_sprite_mxu_rev,.-h1_pgm_sprite_mxu_rev

/* a0=dst, a1=source row, a2=palette, a3=columns; count at o32 sp+16.
 * There is no stack adjustment and no calls, so saved MXU state remains in
 * t6/t7 while t8 holds the fifth argument through the entire mapped span.
 */
.section .text.h1_pgm_sprite_mxu_map,"ax",@progbits
.globl h1_pgm_sprite_mxu_map
.type h1_pgm_sprite_mxu_map,@function
h1_pgm_sprite_mxu_map:
 lw $t8,16($sp)
 move $v0,$zero
 beqz $t8,.Lmxmap_return
 nop
 pgm_mxu_begin
 sltiu $t4,$t8,4
 bnez $t4,.Lmxmap_tail
 nop
.Lmxmap_four:
 lhu $t0,0($a3)
 lhu $t1,2($a3)
 lhu $t2,4($a3)
 lhu $t3,6($a3)
 addu $t0,$a1,$t0
 addu $t1,$a1,$t1
 addu $t2,$a1,$t2
 addu $t3,$a1,$t3
 lbu $t0,0($t0)
 lbu $t1,0($t1)
 lbu $t2,0($t2)
 lbu $t3,0($t3)
 pgm_mxu_store $t0,8,0,$t8,.Lmxmap_skip0
 pgm_mxu_store $t1,9,4,$t8,.Lmxmap_skip1
 pgm_mxu_store $t2,10,8,$t8,.Lmxmap_skip2
 pgm_mxu_store $t3,11,12,$t8,.Lmxmap_skip3
 addiu $a3,$a3,8
 sltiu $t4,$t8,4
 beqz $t4,.Lmxmap_four
 addiu $a0,$a0,16
 beqz $t8,.Lmxmap_done
 nop
.Lmxmap_tail:
 lhu $t0,0($a3)
 addiu $a3,$a3,2
 addu $t0,$a1,$t0
 lbu $t0,0($t0)
 nop
 pgm_mxu_store $t0,8,0,$t8,.Lmxmap_tail_skip
 bnez $t8,.Lmxmap_tail
 addiu $a0,$a0,4
.Lmxmap_done:
 pgm_mxu_end
.Lmxmap_return:
 jr $ra
 nop
.size h1_pgm_sprite_mxu_map,.-h1_pgm_sprite_mxu_map
.set at
.set reorder
