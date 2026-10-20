/* JZ4740 MXU1 leaf routines. GPL-2.0-or-later.
 * Encodings: Ingenic XBurst MXU ISA manual, sections 3.4, 3.7, 3.9-3.11.
 * GNU MIPS32 assemblers need explicit words for this vendor extension.
 * Each bounded call preserves XR1-XR10 and MXU_CR. By default CP0 Status
 * is saved/restored and IRQs are masked only during the leaf: no OS/USB
 * calls or unbounded work inside it. The PGM-only copy32 leaf leaves CP0
 * Status and interrupt enable untouched while preserving the MXU context.
 */
.set mips32
.set noreorder
.macro mx_i2m xr, gr
 .word (0x7000002f | (\gr << 16) | (\xr << 6))
.endm
.macro mx_m2i xr, gr
 .word (0x7000002e | (\gr << 16) | (\xr << 6))
.endm
.macro mx_ld xr, gr, off
 .word (0x70000010 | (\gr << 21) | (((\off / 4) & 1023) << 10) | (\xr << 6))
.endm
.macro mx_st xr, gr, off
 .word (0x70000011 | (\gr << 21) | (((\off / 4) & 1023) << 10) | (\xr << 6))
.endm
.macro mx_shift fn, a, b, c, d, n
 .word (0x70000000 | \fn | (\n << 22) | (\d << 18) | (\c << 14) | (\b << 10) | (\a << 6))
.endm
.macro mx_shuffle a, b, c, d, ptn
 .word (0x7000003d | (\ptn << 24) | (\d << 18) | (\c << 14) | (\b << 10) | (\a << 6))
.endm
.macro mx_begin irq=1
 addiu $sp,$sp,-48
 .if \irq
 mfc0 $t8,$12
 nop
 li $t0,-2
 and $t0,$t8,$t0
 mtc0 $t0,$12
 nop
 nop
 nop
 .endif
 mx_m2i 16,25
 ori $t0,$t9,1
 mx_i2m 16,8
 nop
 nop
 nop
 .irp r,1,2,3,4,5,6,7,8,9,10
  mx_m2i \r,8
  sw $t0,((\r-1)*4)($sp)
 .endr
.endm
.macro mx_end irq=1
 .irp r,1,2,3,4,5,6,7,8,9,10
  lw $t0,((\r-1)*4)($sp)
  mx_i2m \r,8
 .endr
 mx_i2m 16,25
 .if \irq
 mtc0 $t8,$12
 nop
 nop
 nop
 .endif
 jr $ra
 addiu $sp,$sp,48
.endm

.section .text.h1_mxu_control,"ax",@progbits
.globl h1_mxu_control
.type h1_mxu_control,@function
h1_mxu_control:
 mx_m2i 16,2
 jr $ra
 nop
.size h1_mxu_control,.-h1_mxu_control

.section .text.h1_mxu_probe,"ax",@progbits
.globl h1_mxu_probe
.type h1_mxu_probe,@function
h1_mxu_probe:
 mx_begin
 li $t0,0x12345678
 mx_i2m 1,8
 mx_m2i 1,2
 mx_end
.size h1_mxu_probe,.-h1_mxu_probe

/* out: 32-bit aligned; src: 32-bit aligned RGB565; n: 4..480, multiple of 4.
 * Four pixels per iteration, two packed words. The halfword shifts and
 * shuffles do the actual SIMD RGB565 -> 0x00RRGGBB conversion, no color LUT.
 */
.section .text.h1_mxu_rgb565,"ax",@progbits
.globl h1_mxu_rgb565
.type h1_mxu_rgb565,@function
h1_mxu_rgb565:
 mx_begin
1:
 mx_ld 1,5,0
 mx_ld 2,5,4
 /* Extract each channel using Release-1 packed halfword shifts only.
  * The shift-left truncation masks unwanted high channel bits exactly. */
 mx_shift 0x35,3,1,2,4,11
 mx_shift 0x34,3,3,4,4,3
 mx_shift 0x34,5,1,2,6,5
 mx_shift 0x35,5,5,6,6,10
 mx_shift 0x34,5,5,6,6,2
 mx_shift 0x34,1,1,2,2,11
 mx_shift 0x35,1,1,2,2,8
 mx_shuffle 0,5,1,7,2
 mx_shuffle 0,6,2,8,2
 mx_shuffle 5,3,7,1,3
 mx_shuffle 6,4,8,2,3
 mx_st 1,4,0
 mx_st 5,4,4
 mx_st 2,4,8
 mx_st 6,4,12
 addiu $a2,$a2,-4
 addiu $a1,$a1,8
 bnez $a2,1b
 addiu $a0,$a0,16
 mx_end
.size h1_mxu_rgb565,.-h1_mxu_rgb565

/* Both addresses word aligned, n is a positive multiple of 8 words, <=480. */
.section .text.h1_mxu_copy32,"ax",@progbits
.globl h1_mxu_copy32
.type h1_mxu_copy32,@function
h1_mxu_copy32:
 mx_begin 0
1:
 .irp r,1,2,3,4,5,6,7,8
  mx_ld \r,5,((\r-1)*4)
 .endr
 .irp r,1,2,3,4,5,6,7,8
  mx_st \r,4,((\r-1)*4)
 .endr
 addiu $a2,$a2,-8
 addiu $a1,$a1,32
 bnez $a2,1b
 addiu $a0,$a0,32
 mx_end 0
.size h1_mxu_copy32,.-h1_mxu_copy32
/* Pair-table tile composition. D16MADL is Release-1 (2007 manual 1.6.13).
 * Each halfword computes new_color + old_color * keep_old (0 or 1).
 * Color zero entries in the pair table are zero, independent of palette[0].
 * a0=16x16 RGB565 destination, a1=128-byte tile, a2=pair table,
 * a3=flip bits, caller sp+16=keep table. Cached RAM only, pitch=704.
 * Odd halfword addresses use standard MIPS unaligned word instructions.
 */
.macro tile_pair word, shift, offset, odd
 srl $t2,\word,\shift
 andi $t2,$t2,255
 /* R1 S32LDDV: scaled palette lookup directly into MXU registers. */
 .word (0x70000012 | (6 << 21) | (10 << 16) | (2 << 14) | (1 << 6))
 .word (0x70000012 | (15 << 21) | (10 << 16) | (2 << 14) | (3 << 6))
 .if \odd
  lwl $t3,(\offset+3)($a0)
  lwr $t3,\offset($a0)
  mx_i2m 2,11
 .else
  mx_ld 2,4,\offset
 .endif
 .word (0x7000000c | (4 << 18) | (3 << 14) | (2 << 10) | (1 << 6))
 .if \odd
  mx_m2i 4,11
  swl $t3,(\offset+3)($a0)
  swr $t3,\offset($a0)
 .else
  mx_st 4,4,\offset
 .endif
.endm
.macro tile_body flip, odd, label
\label:
 lw $t0,0($a1)
 lw $t1,4($a1)
 or $t2,$t0,$t1
 beqz $t2,\label\()_next
 nop
 .if \flip
  tile_pair $t1,0,0,\odd
  tile_pair $t1,8,4,\odd
  tile_pair $t1,16,8,\odd
  tile_pair $t1,24,12,\odd
  tile_pair $t0,0,16,\odd
  tile_pair $t0,8,20,\odd
  tile_pair $t0,16,24,\odd
  tile_pair $t0,24,28,\odd
 .else
  tile_pair $t0,24,0,\odd
  tile_pair $t0,16,4,\odd
  tile_pair $t0,8,8,\odd
  tile_pair $t0,0,12,\odd
  tile_pair $t1,24,16,\odd
  tile_pair $t1,16,20,\odd
  tile_pair $t1,8,24,\odd
  tile_pair $t1,0,28,\odd
 .endif
\label\()_next:
 addiu $t5,$t5,-1
 addiu $a1,$a1,8
 bnez $t5,\label
 addu $a0,$a0,$t6
 b tile_done
 nop
.endm
.section .text.h1_mxu_tile16,"ax",@progbits
.globl h1_mxu_tile16
.type h1_mxu_tile16,@function
h1_mxu_tile16:
 lw $t7,16($sp)
 mx_begin
 li $t5,16
 li $t6,704
 andi $t0,$a3,2
 beqz $t0,tile_y_ready
 nop
 addiu $a0,$a0,10560
 li $t6,-704
tile_y_ready:
 andi $t0,$a0,3
 andi $t1,$a3,1
 bnez $t0,tile_odd
 nop
 bnez $t1,tile_flip_even
 nop
 tile_body 0,0,tile_normal_even
tile_odd:
 bnez $t1,tile_flip_odd
 nop
 tile_body 0,1,tile_normal_odd
 tile_body 1,0,tile_flip_even
 tile_body 1,1,tile_flip_odd
tile_done:
 mx_end
.size h1_mxu_tile16,.-h1_mxu_tile16

/* Opaque-only paired palette composition. Eligibility is cached with ROM
 * pages. No destination reads, mask loads or multiply are needed here.
 * Only XR1 is touched; preserve exactly that register plus CR and Status.
 */
.macro opaque_pair word, shift, offset, odd
 srl $t2,\word,\shift
 andi $t2,$t2,255
 .word (0x70000012 | (6 << 21) | (10 << 16) | (2 << 14) | (1 << 6))
 .if \odd
  mx_m2i 1,11
  swl $t3,(\offset+3)($a0)
  swr $t3,\offset($a0)
 .else
  mx_st 1,4,\offset
 .endif
.endm
.macro opaque_body flip, odd, label
\label:
 lw $t0,0($a1)
 lw $t1,4($a1)
 .if \flip
  opaque_pair $t1,0,0,\odd
  opaque_pair $t1,8,4,\odd
  opaque_pair $t1,16,8,\odd
  opaque_pair $t1,24,12,\odd
  opaque_pair $t0,0,16,\odd
  opaque_pair $t0,8,20,\odd
  opaque_pair $t0,16,24,\odd
  opaque_pair $t0,24,28,\odd
 .else
  opaque_pair $t0,24,0,\odd
  opaque_pair $t0,16,4,\odd
  opaque_pair $t0,8,8,\odd
  opaque_pair $t0,0,12,\odd
  opaque_pair $t1,24,16,\odd
  opaque_pair $t1,16,20,\odd
  opaque_pair $t1,8,24,\odd
  opaque_pair $t1,0,28,\odd
 .endif
 addiu $t5,$t5,-1
 addiu $a1,$a1,8
 bnez $t5,\label
 addu $a0,$a0,$t6
 b opaque_done
 nop
.endm
.section .text.h1_mxu_tile16_opaque,"ax",@progbits
.globl h1_mxu_tile16_opaque
.type h1_mxu_tile16_opaque,@function
h1_mxu_tile16_opaque:
 addiu $sp,$sp,-16
 mfc0 $t8,$12
 nop
 li $t0,-2
 and $t0,$t8,$t0
 mtc0 $t0,$12
 nop
 nop
 nop
 mx_m2i 16,25
 ori $t0,$t9,1
 mx_i2m 16,8
 nop
 nop
 nop
 mx_m2i 1,8
 sw $t0,0($sp)
 li $t5,16
 li $t6,704
 andi $t0,$a3,2
 beqz $t0,opaque_y_ready
 nop
 addiu $a0,$a0,10560
 li $t6,-704
opaque_y_ready:
 andi $t0,$a0,3
 andi $t1,$a3,1
 bnez $t0,opaque_odd
 nop
 bnez $t1,opaque_flip_even
 nop
 opaque_body 0,0,opaque_normal_even
opaque_odd:
 bnez $t1,opaque_flip_odd
 nop
 opaque_body 0,1,opaque_normal_odd
 opaque_body 1,0,opaque_flip_even
 opaque_body 1,1,opaque_flip_odd
opaque_done:
 lw $t0,0($sp)
 mx_i2m 1,8
 mx_i2m 16,25
 mtc0 $t8,$12
 nop
 nop
 nop
 jr $ra
 addiu $sp,$sp,16
.size h1_mxu_tile16_opaque,.-h1_mxu_tile16_opaque

/* Predecoded RGB565 tile copy/flip. Cached RAM only; a0=dst,a1=src,a2=flips.
 * S32ALN with a register shift of 2 swaps the two halfwords for x-flip.
 * This is Release-1 S32ALN, not the Release-2 immediate/bitwise pool members.
 */
.macro rgb_pair input, output, odd, flip
 mx_ld 1,5,\input
 .if \flip
  .word (0x70000027 | (15 << 21) | (1 << 18) | (1 << 14) | (1 << 10) | (1 << 6))
 .endif
 .if \odd
  mx_m2i 1,11
  swl $t3,(\output+3)($a0)
  swr $t3,\output($a0)
 .else
  mx_st 1,4,\output
 .endif
.endm
.macro rgb_body flip, odd, label
\label:
 .irp n,0,1,2,3,4,5,6,7
  .if \flip
   rgb_pair (28-\n*4),(\n*4),\odd,1
  .else
   rgb_pair (\n*4),(\n*4),\odd,0
  .endif
 .endr
 addiu $t5,$t5,-1
 addiu $a1,$a1,32
 bnez $t5,\label
 addu $a0,$a0,$t6
 b rgb_done
 nop
.endm
.section .text.h1_mxu_tile16_rgb,"ax",@progbits
.globl h1_mxu_tile16_rgb
.type h1_mxu_tile16_rgb,@function
h1_mxu_tile16_rgb:
 addiu $sp,$sp,-16
 mfc0 $t8,$12
 nop
 li $t0,-2
 and $t0,$t8,$t0
 mtc0 $t0,$12
 nop
 nop
 nop
 mx_m2i 16,25
 ori $t0,$t9,1
 mx_i2m 16,8
 nop
 nop
 nop
 mx_m2i 1,8
 sw $t0,0($sp)
 li $t5,16
 li $t6,704
 li $t7,2
 andi $t0,$a2,2
 beqz $t0,rgb_y_ready
 nop
 addiu $a0,$a0,10560
 li $t6,-704
rgb_y_ready:
 andi $t0,$a0,3
 andi $t1,$a2,1
 bnez $t0,rgb_odd
 nop
 bnez $t1,rgb_flip_even
 nop
 rgb_body 0,0,rgb_normal_even
rgb_odd:
 bnez $t1,rgb_flip_odd
 nop
 rgb_body 0,1,rgb_normal_odd
 rgb_body 1,0,rgb_flip_even
 rgb_body 1,1,rgb_flip_odd
rgb_done:
 lw $t0,0($sp)
 mx_i2m 1,8
 mx_i2m 16,25
 mtc0 $t8,$12
 nop
 nop
 nop
 jr $ra
 addiu $sp,$sp,16
.size h1_mxu_tile16_rgb,.-h1_mxu_tile16_rgb

/* Original 64-byte palette stays hot. Two scaled MXU loads and one shuffle
 * pack two RGB565 pixels without expanded palette or decoded-tile tables. */
.macro palette_pair word, first, second, output, odd
 srl $t2,\word,\first
 andi $t2,$t2,15
 .word (0x70000012 | (6 << 21) | (10 << 16) | (2 << 14) | (1 << 6))
 srl $t2,\word,\second
 andi $t2,$t2,15
 .word (0x70000012 | (6 << 21) | (10 << 16) | (2 << 14) | (2 << 6))
 mx_shuffle 0,2,1,3,3 /* PTN3 packs the two low halfwords */
 .if \odd
  mx_m2i 3,11
  swl $t3,(\output+3)($a0)
  swr $t3,\output($a0)
 .else
  mx_st 3,4,\output
 .endif
.endm
.macro palette_body flip, odd, label
\label:
 lw $t0,0($a1)
 lw $t1,4($a1)
 .if \flip
  palette_pair $t1,0,4,0,\odd
  palette_pair $t1,8,12,4,\odd
  palette_pair $t1,16,20,8,\odd
  palette_pair $t1,24,28,12,\odd
  palette_pair $t0,0,4,16,\odd
  palette_pair $t0,8,12,20,\odd
  palette_pair $t0,16,20,24,\odd
  palette_pair $t0,24,28,28,\odd
 .else
  palette_pair $t0,28,24,0,\odd
  palette_pair $t0,20,16,4,\odd
  palette_pair $t0,12,8,8,\odd
  palette_pair $t0,4,0,12,\odd
  palette_pair $t1,28,24,16,\odd
  palette_pair $t1,20,16,20,\odd
  palette_pair $t1,12,8,24,\odd
  palette_pair $t1,4,0,28,\odd
 .endif
 addiu $t5,$t5,-1
 addiu $a1,$a1,8
 bnez $t5,\label
 addu $a0,$a0,$t6
 b palette_done
 nop
.endm
/* Load eight colors before shuffling, then store four pairs. Separating
 * dependent MXU loads/shuffles/stores avoids a dependency chain per pair. */
.ifndef H1_TILE_COMPACT
.set H1_TILE_COMPACT,0
.endif
.macro palette_eight word, output, odd, flip
 .irp r,1,2,3,4,5,6,7,8
  .if \flip
   srl $t2,\word,((\r-1)*4)
  .else
   srl $t2,\word,(32-\r*4)
  .endif
  andi $t2,$t2,15
  .word (0x70000012 | (6 << 21) | (10 << 16) | (2 << 14) | (\r << 6))
 .endr
 .if H1_TILE_COMPACT
 mx_shuffle 0,2,1,1,3
 mx_shuffle 0,4,3,3,3
 mx_shuffle 0,6,5,5,3
 mx_shuffle 0,8,7,7,3
 .else
 mx_shuffle 0,2,1,9,3
 mx_shuffle 0,4,3,10,3
 mx_shuffle 0,6,5,11,3
 mx_shuffle 0,8,7,12,3
 .endif
 .if \odd
  .if H1_TILE_COMPACT
  mx_m2i 1,10
  mx_m2i 3,11
  mx_m2i 5,12
  mx_m2i 7,15
  .else
  mx_m2i 9,10
  mx_m2i 10,11
  mx_m2i 11,12
  mx_m2i 12,15
  .endif
  swl $t2,(\output+3)($a0)
  swr $t2,\output($a0)
  swl $t3,(\output+7)($a0)
  swr $t3,(\output+4)($a0)
  swl $t4,(\output+11)($a0)
  swr $t4,(\output+8)($a0)
  swl $t7,(\output+15)($a0)
  swr $t7,(\output+12)($a0)
 .else
  .if H1_TILE_COMPACT
  mx_st 1,4,\output
  mx_st 3,4,(\output+4)
  mx_st 5,4,(\output+8)
  mx_st 7,4,(\output+12)
  .else
  mx_st 9,4,\output
  mx_st 10,4,(\output+4)
  mx_st 11,4,(\output+8)
  mx_st 12,4,(\output+12)
  .endif
 .endif
.endm
.macro palette_pipeline flip, odd, label
\label:
 lw $t0,0($a1)
 lw $t1,4($a1)
 .if \flip
  palette_eight $t1,0,\odd,1
  palette_eight $t0,16,\odd,1
 .else
  palette_eight $t0,0,\odd,0
  palette_eight $t1,16,\odd,0
 .endif
 addiu $t5,$t5,-1
 addiu $a1,$a1,8
 bnez $t5,\label
 addu $a0,$a0,$t6
 b palette_done
 nop
.endm
.section .text.h1_mxu_tile16_palette,"ax",@progbits
.globl h1_mxu_tile16_palette
.type h1_mxu_tile16_palette,@function
h1_mxu_tile16_palette:
 addiu $sp,$sp,-64
 mfc0 $t8,$12
 nop
 li $t0,-2
 and $t0,$t8,$t0
 mtc0 $t0,$12
 nop
 nop
 nop
 mx_m2i 16,25
 ori $t0,$t9,1
 mx_i2m 16,8
 nop
 nop
 nop
 .if H1_TILE_COMPACT
 .irp r,1,2,3,4,5,6,7,8
  mx_m2i \r,8
  sw $t0,((\r-1)*4)($sp)
 .endr
 .else
 .irp r,1,2,3,4,5,6,7,8,9,10,11,12
  mx_m2i \r,8
  sw $t0,((\r-1)*4)($sp)
 .endr
 .endif
 li $t5,16
 li $t6,704
 andi $t0,$a3,2
 beqz $t0,palette_y_ready
 nop
 addiu $a0,$a0,10560
 li $t6,-704
palette_y_ready:
 andi $t0,$a0,3
 andi $t1,$a3,1
 bnez $t0,palette_odd
 nop
 bnez $t1,palette_flip_even
 nop
 palette_pipeline 0,0,palette_normal_even
palette_odd:
 bnez $t1,palette_flip_odd
 nop
 palette_pipeline 0,1,palette_normal_odd
 palette_pipeline 1,0,palette_flip_even
 palette_pipeline 1,1,palette_flip_odd
palette_done:
 .if H1_TILE_COMPACT
 .irp r,1,2,3,4,5,6,7,8
  lw $t0,((\r-1)*4)($sp)
  mx_i2m \r,8
 .endr
 .else
 .irp r,1,2,3,4,5,6,7,8,9,10,11,12
  lw $t0,((\r-1)*4)($sp)
  mx_i2m \r,8
 .endr
 .endif
 mx_i2m 16,25
 mtc0 $t8,$12
 nop
 nop
 nop
 jr $ra
 addiu $sp,$sp,64
.size h1_mxu_tile16_palette,.-h1_mxu_tile16_palette
.set reorder
