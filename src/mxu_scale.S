/* Exact 320 -> 362 nearest-neighbor expansion, JZ4740 MXU Release-1.
 * a0 = word-aligned RGB32 destination, a1 = word-aligned RGB565 source,
 * a2 = 20-bit mask of changed 16-source-pixel blocks. No other pixels touched.
 * Two identical 160 -> 181 halves let assembler constants select repeated
 * pixels directly from converted XR registers: no gather or RGB32 row buffer.
 * Each bounded row call preserves XR1-XR10, CR and CP0 Status.
 * GPL-2.0-or-later. Conversion encodings match the audited mxu.S kernel.
 */
.set mips32
.set noreorder
.macro sc_i2m xr,gr
 .word (0x7000002f | (\gr << 16) | (\xr << 6))
.endm
.macro sc_m2i xr,gr
 .word (0x7000002e | (\gr << 16) | (\xr << 6))
.endm
.macro sc_ld xr,gr,off
 .word (0x70000010 | (\gr << 21) | ((((\off) / 4) & 1023) << 10) | (\xr << 6))
.endm
.macro sc_st xr,gr,off
 .word (0x70000011 | (\gr << 21) | ((((\off) / 4) & 1023) << 10) | (\xr << 6))
.endm
.macro sc_shift fn,a,b,c,d,n
 .word (0x70000000 | \fn | (\n << 22) | (\d << 18) | (\c << 14) | (\b << 10) | (\a << 6))
.endm
.macro sc_shuffle a,b,c,d,ptn
 .word (0x7000003d | (\ptn << 24) | (\d << 18) | (\c << 14) | (\b << 10) | (\a << 6))
.endm
.macro sc_block block
 andi $t0,$a2,(1 << \block)
 beqz $t0,.Lsc_skip\@
 nop
 .set sc_source,\block*16
 .rept 4
  sc_ld 1,5,sc_source*2
  sc_ld 2,5,sc_source*2+4
  sc_shift 0x35,3,1,2,4,11
  sc_shift 0x34,3,3,4,4,3
  sc_shift 0x34,5,1,2,6,5
  sc_shift 0x35,5,5,6,6,10
  sc_shift 0x34,5,5,6,6,2
  sc_shift 0x34,1,1,2,2,11
  sc_shift 0x35,1,1,2,2,8
  sc_shuffle 0,5,1,7,2
  sc_shuffle 0,6,2,8,2
  sc_shuffle 5,3,7,1,3
  sc_shuffle 6,4,8,2,3
  .set sc_output,(sc_source*181+159)/160
  .set sc_end,((sc_source+4)*181+159)/160
  .rept sc_end-sc_output
   .set sc_pixel,(sc_output*160)/181-sc_source
   .if sc_pixel == 0
    sc_st 1,4,sc_output*4
   .elseif sc_pixel == 1
    sc_st 5,4,sc_output*4
   .elseif sc_pixel == 2
    sc_st 2,4,sc_output*4
   .elseif sc_pixel == 3
    sc_st 6,4,sc_output*4
   .else
    .error "scaled pixel escaped source quartet"
   .endif
   .set sc_output,sc_output+1
  .endr
  .set sc_source,sc_source+4
 .endr
.Lsc_skip\@:
.endm
.section .text.h1_mxu_scale362,"ax",@progbits
.globl h1_mxu_scale362
.type h1_mxu_scale362,@function
h1_mxu_scale362:
 addiu $sp,$sp,-48
 mfc0 $t8,$12
 nop
 li $t0,-2
 and $t0,$t8,$t0
 mtc0 $t0,$12
 nop
 nop
 nop
 sc_m2i 16,25
 ori $t0,$t9,1
 sc_i2m 16,8
 nop
 nop
 nop
 .irp r,1,2,3,4,5,6,7,8,9,10
  sc_m2i \r,8
  sw $t0,((\r-1)*4)($sp)
 .endr
 li $t1,2
.Lsc_half:
 andi $t0,$a2,1023
 beqz $t0,.Lsc_next_half
 nop
 sc_block 0
 sc_block 1
 sc_block 2
 sc_block 3
 sc_block 4
 sc_block 5
 sc_block 6
 sc_block 7
 sc_block 8
 sc_block 9
.Lsc_next_half:
 addiu $a1,$a1,320
 addiu $a0,$a0,724
 srl $a2,$a2,10
 addiu $t1,$t1,-1
 bnez $t1,.Lsc_half
 nop
 .irp r,1,2,3,4,5,6,7,8,9,10
  lw $t0,((\r-1)*4)($sp)
  sc_i2m \r,8
 .endr
 sc_i2m 16,25
 mtc0 $t8,$12
 nop
 nop
 nop
 jr $ra
 addiu $sp,$sp,48
.size h1_mxu_scale362,.-h1_mxu_scale362
