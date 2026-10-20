/* PGM indexed RGB32 row composition, MIPS32r1. GPL-2.0-or-later.
 * Palette indices 0..31 are opaque; 255 preserves the destination.
 * Only ordinary cached RAM is accessed. No MXU, CP0, or IRQ state changes.
 * Both leaves accept count == 0, preserve the o32 callee-saved registers,
 * and return the number of opaque stores in v0 (callers may ignore it).
 * dst and palette are word aligned; columns is halfword aligned.
 */
.set mips32
.set noreorder
.set noat

/* The four index loads precede their dependent palette loads. Transparent
 * entries never read palette[255] or load/store the destination. The count
 * increment also separates each palette load from its dependent store.
 */
.macro pgm_store index, offset, skip
 beq \index,$t9,\skip
 sll \index,\index,2
 addu \index,$a2,\index
 lw \index,0(\index)
 addiu $v0,$v0,1
 sw \index,\offset($a0)
\skip:
.endm

/* a0=dst, a1=contiguous source bytes, a2=32-entry palette, a3=count. */
.section .text.h1_pgm_sprite_row,"ax",@progbits
.globl h1_pgm_sprite_row
.type h1_pgm_sprite_row,@function
h1_pgm_sprite_row:
 beqz $a3,.Lrow_return
 move $v0,$zero
 li $t9,255
 sltiu $t4,$a3,4
 bnez $t4,.Lrow_tail
 nop
.Lrow_four:
 lbu $t0,0($a1)
 lbu $t1,1($a1)
 lbu $t2,2($a1)
 lbu $t3,3($a1)
 pgm_store $t0,0,.Lrow_skip0
 pgm_store $t1,4,.Lrow_skip1
 pgm_store $t2,8,.Lrow_skip2
 pgm_store $t3,12,.Lrow_skip3
 addiu $a3,$a3,-4
 addiu $a1,$a1,4
 sltiu $t4,$a3,4
 beqz $t4,.Lrow_four
 addiu $a0,$a0,16
 beqz $a3,.Lrow_return
 nop
.Lrow_tail:
 lbu $t0,0($a1)
 addiu $a1,$a1,1
 pgm_store $t0,0,.Lrow_tail_skip
 addiu $a3,$a3,-1
 bnez $a3,.Lrow_tail
 addiu $a0,$a0,4
.Lrow_return:
 jr $ra
 nop
.size h1_pgm_sprite_row,.-h1_pgm_sprite_row

/* a0=dst, a1=rightmost source byte, a2=palette, a3=count.
 * The source decrements while the destination advances, for identity zoom
 * with horizontal flip. Reads stop exactly after count source bytes.
 */
.section .text.h1_pgm_sprite_rev,"ax",@progbits
.globl h1_pgm_sprite_rev
.type h1_pgm_sprite_rev,@function
h1_pgm_sprite_rev:
 beqz $a3,.Lrev_return
 move $v0,$zero
 li $t9,255
 sltiu $t4,$a3,4
 bnez $t4,.Lrev_tail
 nop
.Lrev_four:
 lbu $t0,0($a1)
 lbu $t1,-1($a1)
 lbu $t2,-2($a1)
 lbu $t3,-3($a1)
 pgm_store $t0,0,.Lrev_skip0
 pgm_store $t1,4,.Lrev_skip1
 pgm_store $t2,8,.Lrev_skip2
 pgm_store $t3,12,.Lrev_skip3
 addiu $a3,$a3,-4
 addiu $a1,$a1,-4
 sltiu $t4,$a3,4
 beqz $t4,.Lrev_four
 addiu $a0,$a0,16
 beqz $a3,.Lrev_return
 nop
.Lrev_tail:
 lbu $t0,0($a1)
 addiu $a1,$a1,-1
 pgm_store $t0,0,.Lrev_tail_skip
 addiu $a3,$a3,-1
 bnez $a3,.Lrev_tail
 addiu $a0,$a0,4
.Lrev_return:
 jr $ra
 nop
.size h1_pgm_sprite_rev,.-h1_pgm_sprite_rev

/* a0=dst, a1=source row, a2=palette, a3=column map; count at sp+16.
 * No stack adjustment is needed, so the fifth argument retains its o32
 * entry offset. Source/map reads are bounded by count, including the tail.
 */
.section .text.h1_pgm_sprite_map,"ax",@progbits
.globl h1_pgm_sprite_map
.type h1_pgm_sprite_map,@function
h1_pgm_sprite_map:
 lw $t8,16($sp)
 move $v0,$zero
 beqz $t8,.Lmap_return
 li $t9,255
 sltiu $t4,$t8,4
 bnez $t4,.Lmap_tail
 nop
.Lmap_four:
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
 pgm_store $t0,0,.Lmap_skip0
 pgm_store $t1,4,.Lmap_skip1
 pgm_store $t2,8,.Lmap_skip2
 pgm_store $t3,12,.Lmap_skip3
 addiu $t8,$t8,-4
 addiu $a3,$a3,8
 sltiu $t4,$t8,4
 beqz $t4,.Lmap_four
 addiu $a0,$a0,16
 beqz $t8,.Lmap_return
 nop
.Lmap_tail:
 lhu $t0,0($a3)
 addiu $a3,$a3,2
 addu $t0,$a1,$t0
 lbu $t0,0($t0)
 nop
 pgm_store $t0,0,.Lmap_tail_skip
 addiu $t8,$t8,-1
 bnez $t8,.Lmap_tail
 addiu $a0,$a0,4
.Lmap_return:
 jr $ra
 nop
.size h1_pgm_sprite_map,.-h1_pgm_sprite_map
.set at
.set reorder
