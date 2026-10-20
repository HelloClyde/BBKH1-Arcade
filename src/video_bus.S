/* Shared RAM stores and exact 0x3c0000/2/4 word ports. Private ABI:
 * AT=guest address, T9=value on entry / handled flag on return.
 * Every other register, including V0 and the original operands, is preserved. */
.set noreorder
.set noat
.text
.macro bus_writer name,width
.globl \name
.type \name,@function
\name:
 addiu $sp,$sp,-32
 sw $t0,0($sp)
 sw $t1,4($sp)
 sll $t0,$at,8
 srl $t0,$t0,8
#if H1_PGM
 lui $t1,%hi(h1_pgm_bus_pages)
 lw $t1,%lo(h1_pgm_bus_pages)($t1)
 nop
 beqz $t1,.Lneo_write_\name
 nop
 /* PGM: only ordinary mirrored 128 KiB work RAM may bypass callbacks. */
 lui $t1,%hi(h1_bus_fast)
 lw $t1,%lo(h1_bus_fast)($t1)
 nop
 beqz $t1,.Lwrite_fail
 srl $t1,$t0,20
 addiu $t1,$t1,-8
 bnez $t1,.Lwrite_fail
 nop
 .if \width != 1
 andi $t1,$t0,1
 bnez $t1,.Lwrite_fail
 nop
 .endif
 sll $t0,$t0,15
 srl $t0,$t0,15
 .if \width == 4
 li $t1,131068
 sltu $t1,$t1,$t0
 bnez $t1,.Lwrite_fail
 nop
 .endif
 lui $t1,%hi(h1_pgm_bus_pages)
 lw $t1,%lo(h1_pgm_bus_pages)($t1)
 nop
 lw $t1,512($t1)
 b .Lram_write_\name
 addu $t0,$t0,$t1
.Lneo_write_\name:
#endif
 srl $t1,$t0,20
 addiu $t1,$t1,-1
 bnez $t1,.Lnonram_\name
 nop
 .if \width != 1
 andi $t1,$t0,1
 bnez $t1,.Lwrite_fail
 nop
 .endif
 andi $t0,$t0,65535
 .if \width == 4
 ori $t1,$zero,65532
 sltu $t1,$t1,$t0
 bnez $t1,.Lwrite_fail
 nop
 .endif
 lui $t1,%hi(h1_bus_ram)
 lw $t1,%lo(h1_bus_ram)($t1)
 nop
 beqz $t1,.Lwrite_fail
 addu $t0,$t0,$t1
.Lram_write_\name:
 .if \width == 1
 xori $t0,$t0,1
 sb $t9,0($t0)
 .elseif \width == 2
 sh $t9,0($t0)
 .else
 srl $t1,$t9,16
 sh $t1,0($t0)
 sh $t9,2($t0)
 .endif
 b .Lwrite_restore
 li $t9,1
.Lnonram_\name:
 .if \width == 2
 lui $t1,60
 subu $t0,$t0,$t1
 sltiu $t1,$t0,5
 beqz $t1,.Lwrite_fail
 andi $t1,$t0,1
 bnez $t1,.Lwrite_fail
 lui $t1,%hi(h1_video_bus_enabled)
 lw $t1,%lo(h1_video_bus_enabled)($t1)
 nop
 beqz $t1,.Lwrite_fail
 nop
 /* Only the uncommon VRAM path needs the remaining three temporaries. */
 sw $t2,8($sp)
 sw $t3,12($sp)
 sw $t4,16($sp)
 move $at,$t9
 move $t9,$t0
 b .Lvram_word
 nop
 .else
 b .Lwrite_fail
 nop
 .endif
.size \name,.-\name
.endm
bus_writer h1_bus_write8,1
bus_writer h1_bus_write16,2
bus_writer h1_bus_write32,4
.Lvram_word:
 lui $t0,%hi(h1_vram_descriptor)
 addiu $t0,$t0,%lo(h1_vram_descriptor)
 lw $t1,4($t0)
 lw $t3,0($t0)
 lw $t2,0($t1)
 beqz $t9,.Lvram_address
 addiu $t4,$t9,-2
 beqz $t4,.Lvram_data
 nop
 /* Modulo: bit14 is replicated to bit15, as in the original callback. */
 andi $t2,$at,32767
 andi $t4,$at,16384
 sll $t4,$t4,1
 or $t2,$t2,$t4
 lw $t1,8($t0)
 b .Lvram_count
 sh $t2,0($t1)
.Lvram_address:
 andi $t2,$at,65535
 b .Lvram_refill
 sw $t2,0($t1)
.Lvram_data:
 sll $t4,$t2,1
 addu $t4,$t3,$t4
 sh $at,0($t4)
 lw $t4,8($t0)
 lh $t4,0($t4)
 addu $t4,$t4,$t2
 andi $t2,$t2,32768
 andi $t4,$t4,32767
 addu $t2,$t2,$t4
 sw $t2,0($t1)
.Lvram_refill:
 sll $t2,$t2,1
 addu $t3,$t3,$t2
 lhu $t2,0($t3)
 lw $t1,12($t0)
 sh $t2,0($t1)
.Lvram_count:
 lw $t1,16($t0)
 lw $t2,0($t1)
 addiu $t2,$t2,1
 sw $t2,0($t1)
 lw $t2,8($sp)
 lw $t3,12($sp)
 lw $t4,16($sp)
 b .Lwrite_restore
 li $t9,1
.Lwrite_fail:
 move $t9,$zero
.Lwrite_restore:
 lw $t0,0($sp)
 lw $t1,4($sp)
 jr $ra
 addiu $sp,$sp,32
/* Shared data-ROM read leaves: AT=guest address, V0=result, T9=hit flag.
 * Only those scratch registers and RA are modified. Other bus reads fall
 * through the original interpreter callbacks without changing their inputs. */
.macro rom_reader name,width
.section .text.\name,"ax",@progbits
.globl \name
.type \name,@function
\name:
 lui $v0,%hi(h1_bus_rom_active)
 lw $v0,%lo(h1_bus_rom_active)($v0)
 nop
 beqz $v0,.Lfail_\name
 srl $v0,$at,20
#if H1_PGM
 lui $t9,%hi(h1_pgm_bus_pages)
 lw $t9,%lo(h1_pgm_bus_pages)($t9)
 nop
 beqz $t9,.Lneo_rom_\name
 nop
 srl $v0,$at,16
 andi $v0,$v0,255
 sll $v0,$v0,2
 addu $t9,$t9,$v0
 lw $t9,0($t9)
 nop
 beqz $t9,.Lfail_\name
 nop
 .if \width != 1
 andi $v0,$at,1
 bnez $v0,.Lfail_\name
 nop
 .endif
 andi $at,$at,65535
 .if \width == 4
 ori $v0,$zero,65532
 sltu $v0,$v0,$at
 bnez $v0,.Lfail_\name
 nop
 .endif
 b .Lpgm_load_\name
 addu $at,$at,$t9
.Lneo_rom_\name:
 srl $v0,$at,20
#endif
 andi $v0,$v0,15
 lui $t9,%hi(h1_bus_rom_map)
 beqz $v0,.Lselected_\name
 addiu $t9,$t9,%lo(h1_bus_rom_map)
 addiu $v0,$v0,-2
 bnez $v0,.Lfail_\name
 addiu $t9,$t9,8
.Lselected_\name:
 .if \width != 1
 andi $v0,$at,1
 bnez $v0,.Lfail_\name
 nop
 .endif
 sll $at,$at,12
 srl $at,$at,12
 lw $v0,4($t9)
 lw $t9,0($t9)
 addiu $v0,$v0,-\width
 sltu $v0,$v0,$at
 bnez $v0,.Lfail_\name
 nop
 beqz $t9,.Lfail_\name
 addu $at,$at,$t9
.Lpgm_load_\name:
 .if \width == 1
 xori $at,$at,1
 lbu $v0,0($at)
 .elseif \width == 2
 lhu $v0,0($at)
 .else
 lhu $v0,0($at)
 lhu $t9,2($at)
 sll $v0,$v0,16
 or $v0,$v0,$t9
 .endif
 jr $ra
 li $t9,1
.Lfail_\name:
 jr $ra
 move $t9,$zero
.size \name,.-\name
.endm
rom_reader h1_rom_read8,1
rom_reader h1_rom_read16,2
rom_reader h1_rom_read32,4
.set at
.set reorder
