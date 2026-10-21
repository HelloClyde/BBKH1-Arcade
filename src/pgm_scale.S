/* Exact RGB32 nearest-neighbour scaling, ordinary MIPS32 Release 1.
 * a0 = word-aligned destination, a1 = word-aligned 448-pixel source row.
 * Fixed load offsets remove the per-pixel column lookup, shift and add.
 * Registers t0-t7, v0, a0, a1 are caller-saved; no MXU or CP0 state changes.
 * GPL-2.0-or-later.
 */
.set mips32
.set noreorder

.macro pgm_scale_batch base,count,numerator,denominator
 .set pgm_scale_n,\base
 .irp r,$t0,$t1,$t2,$t3,$t4,$t5,$t6,$t7
  .if pgm_scale_n < (\base+\count)
   lw \r,(((pgm_scale_n*\numerator)/\denominator)*4)($a1)
  .endif
  .set pgm_scale_n,pgm_scale_n+1
 .endr
 .set pgm_scale_n,\base
 .irp r,$t0,$t1,$t2,$t3,$t4,$t5,$t6,$t7
  .if pgm_scale_n < (\base+\count)
   sw \r,(pgm_scale_n*4)($a0)
  .endif
  .set pgm_scale_n,pgm_scale_n+1
 .endr
.endm

/* 448/362 = 224/181: identical fixed sampling in two row halves. */
.section .text.h1_pgm_scale36232,"ax",@progbits
.globl h1_pgm_scale36232
.type h1_pgm_scale36232,@function
h1_pgm_scale36232:
 li $v0,2
.Lpgm_scale362_half:
 .set pgm_scale_base,0
 .rept 22
  pgm_scale_batch pgm_scale_base,8,224,181
  .set pgm_scale_base,pgm_scale_base+8
 .endr
 pgm_scale_batch 176,5,224,181
 addiu $a1,$a1,896
 addiu $v0,$v0,-1
 bnez $v0,.Lpgm_scale362_half
 addiu $a0,$a0,724
 jr $ra
 nop
.size h1_pgm_scale36232,.-h1_pgm_scale36232

/* 448/480 = 14/15: repeat the compact 15-pixel pattern 32 times. */
.section .text.h1_pgm_scale48032,"ax",@progbits
.globl h1_pgm_scale48032
.type h1_pgm_scale48032,@function
h1_pgm_scale48032:
 li $v0,32
.Lpgm_scale480_block:
 pgm_scale_batch 0,8,14,15
 pgm_scale_batch 8,7,14,15
 addiu $a1,$a1,56
 addiu $v0,$v0,-1
 bnez $v0,.Lpgm_scale480_block
 addiu $a0,$a0,60
 jr $ra
 nop
.size h1_pgm_scale48032,.-h1_pgm_scale48032
