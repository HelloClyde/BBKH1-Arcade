/* Bounded MXU1 indexed RGB32 tile writer; preserves XR1..8, CR and Status. */
.set mips32
.set noreorder
.macro p32_m2i xr, gr
 .word (0x7000002e | (\gr << 16) | (\xr << 6))
.endm
.macro p32_i2m xr, gr
 .word (0x7000002f | (\gr << 16) | (\xr << 6))
.endm
.macro p32_eight word, off, flip
 .irp r,1,2,3,4,5,6,7,8
  .if \flip
   srl $t2,\word,((\r-1)*4)
  .else
   srl $t2,\word,(32-\r*4)
  .endif
  andi $t2,$t2,15
  .word (0x70000012 | (6 << 21) | (10 << 16) | (2 << 14) | (\r << 6))
 .endr
 .irp r,1,2,3,4,5,6,7,8
  .word (0x70000011 | (4 << 21) | (((\off/4+\r-1)&1023)<<10) | (\r<<6))
 .endr
.endm
.macro p32_loop flip, label
\label:
 lw $t0,0($a1)
 lw $t1,4($a1)
 .if \flip
  p32_eight $t1,0,1
  p32_eight $t0,32,1
 .else
  p32_eight $t0,0,0
  p32_eight $t1,32,0
 .endif
 addiu $t5,$t5,-1
 addiu $a1,$a1,8
 bnez $t5,\label
 addu $a0,$a0,$t6
 b p32_done
 nop
.endm
.section .text.h1_mxu_tile16_palette32,"ax",@progbits
.globl h1_mxu_tile16_palette32
.type h1_mxu_tile16_palette32,@function
h1_mxu_tile16_palette32:
 addiu $sp,$sp,-32
 mfc0 $t8,$12
 nop
 li $t0,-2
 and $t0,$t8,$t0
 mtc0 $t0,$12
 nop
 nop
 nop
 p32_m2i 16,25
 ori $t0,$t9,1
 p32_i2m 16,8
 nop
 nop
 nop
 .irp r,1,2,3,4,5,6,7,8
  p32_m2i \r,8
  sw $t0,((\r-1)*4)($sp)
 .endr
 li $t5,16
 li $t6,1408
 andi $t0,$a3,2
 beqz $t0,p32_y_ready
 nop
 addiu $a0,$a0,21120
 li $t6,-1408
p32_y_ready:
 andi $t0,$a3,1
 bnez $t0,p32_flip
 nop
 p32_loop 0,p32_normal
 p32_loop 1,p32_flip
p32_done:
 .irp r,1,2,3,4,5,6,7,8
  lw $t0,((\r-1)*4)($sp)
  p32_i2m \r,8
 .endr
 p32_i2m 16,25
 mtc0 $t8,$12
 nop
 nop
 nop
 jr $ra
 addiu $sp,$sp,32
.size h1_mxu_tile16_palette32,.-h1_mxu_tile16_palette32
.set reorder
