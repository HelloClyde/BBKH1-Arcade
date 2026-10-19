/* Original 68000 instructions for on-device RAM bus differential tests. */
.text
.globl bus_sequence
bus_sequence:
 move.b %d0,(%a0)
 move.w %d1,2(%a0)
 move.l %d2,4(%a0)
 move.b (%a0),%d3
 move.w 2(%a0),%d4
 move.l 4(%a0),%d5
 add.b %d6,(%a0)
 eor.w %d7,2(%a0)
 add.l %d0,4(%a0)
 bchg #3,(%a0)
 tas (%a0)
 movem.l %d0-%d7/%a2-%a6,16(%a0)
 movem.l 16(%a0),%d0-%d7/%a2-%a6
 move.l %d1,-(%a1)
 move.l (%a1)+,%d6
 cmpm.b (%a0)+,(%a1)+
 stop #0x2700
.globl bus_boundary
bus_boundary:
 move.b %d0,(%a0)
 move.w %d1,(%a0)
 move.l %d2,(%a0)
 move.b (%a0),%d3
 move.w (%a0),%d4
 move.l (%a0),%d5
 stop #0x2700
