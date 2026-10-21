"""Audit the linked JZ4740 leaf functions, not a CPU/instruction simulator.

Reference: Ingenic SIMD/DSP ISA, 2007 revision 1.1, Release-1 instruction
summary (unstarred entries); all four S32 bitwise operations are Release-2.
https://www.sccs.swarthmore.edu/users/16/mmcconv1/jz-simd-docs.pdf
"""
import struct

def audit(payload,functions,base):
    checked=[];total=0
    for name,address,size in functions:
        if name not in ('h1_mxu_control','h1_mxu_probe','h1_mxu_rgb565','h1_mxu_scale362','h1_mxu_copy32','h1_mxu_tile16','h1_mxu_tile16_opaque','h1_mxu_tile16_rgb','h1_mxu_tile16_palette','h1_mxu_tile16_palette32','h1_pgm_sprite_mxu_row','h1_pgm_sprite_mxu_rev','h1_pgm_sprite_mxu_map','h1_pgm_bg_mxu_span'):continue
        assert size>0 and size%4==0 and 0<=address-base<=len(payload)-size
        count=0;source_words=[];destination_words=[]
        for off in range(address-base,address-base+size,4):
            word=struct.unpack_from('<I',payload,off)[0]
            if name=='h1_mxu_copy32' or name.startswith('h1_pgm_sprite_mxu_') or name=='h1_pgm_bg_mxu_span':
                assert word>>26!=0x10,'PGM MXU leaves must not read/write CP0 interrupt state'
            if word>>26!=0x1c:continue
            op=word&63
            if op==0x27 and word>>18&7!=1:raise ValueError('only R1 S32ALN permitted in pool16')
            if op not in (0x0c,0x10,0x11,0x12,0x27,0x2e,0x2f,0x34,0x35,0x3d):
                raise ValueError(f'{name}: unaudited or post-MXU1 instruction {word:08x}')
            if op==0x0c:assert word>>22&15==0,'tile D16MADL must use AA,WW'
            if op==0x12:assert word>>10&63==32,'only R1 nonreversed S32LDDV stride 2'
            if op in (0x10,0x11):assert not(word&(1<<20)),'Release-2 reversed load/store'
            if name=='h1_mxu_scale362' and op in (0x10,0x11):
                assert word>>21&31==(5 if op==0x10 else 4)
                (source_words if op==0x10 else destination_words).append(word>>10&1023)
            if op in (0x2e,0x2f):assert word>>6&31<=16
            count+=1
        if name=='h1_mxu_scale362':
            assert source_words==list(range(80)),'scaled source offsets must cover exactly 160 pixels'
            assert destination_words==list(range(181)),'scaled output offsets must cover exactly 181 pixels'
        total+=count;checked.append({'name':name,'bytes':size,'extension_instructions':count})
    assert any(f['name']=='h1_mxu_rgb565' for f in checked)
    return {'ISA':'JZ4740 MXU Release-1, explicit audited subset','functions':checked,
            'static_extension_instructions':total,'post_release1_rejected':True}

def selftest():
    for word in (0x701c84e7,0x70164467,0x70165127,0x7000000b):
        try:audit(struct.pack('<I',word),[('h1_mxu_rgb565',0,4)],0)
        except ValueError:pass
        else:raise AssertionError(f'accepted unsupported instruction {word:08x}')
    assert audit(struct.pack('<I',0x7002006e),[('h1_mxu_rgb565',0,4)],0)['static_extension_instructions']==1
    return {'ok':True,'rejected_bitwise_and_other_extensions':4,'method':'static machine-word audit, no execution model'}

if __name__=='__main__':print(selftest())
