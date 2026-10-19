"""Original 68000/Z80 Neo Geo test cartridge and original test BIOS."""
from pathlib import Path
import struct,sys
from PIL import Image,ImageDraw,ImageFont
from convert import write_package
ROOT=Path(__file__).resolve().parents[1]

def make_regions(large=False):
    main=bytearray(0x100000);bios=bytearray(0x20000)
    struct.pack_into('>II',main,0,0x10ff00,0x100)
    for vector in range(2,32):struct.pack_into('>I',main,vector*4,0x8000)
    bios[:128]=main[:128]
    code=bytearray()
    def word(v):code.extend(struct.pack('>H',v))
    def long(v):code.extend(struct.pack('>I',v))
    def mw(v,a):word(0x33fc);word(v);long(a)
    def mb(v,a):word(0x13fc);word(v);long(a)
    # Fixed graphics use cartridge S1; select cartridge interrupt vectors.
    mb(0,0x3a001b);mb(0,0x3a0013)
    mw(1,0x3c0004)
    for i,c in enumerate((0,0x7fff,0x07f0,0x0f80,0x088f,0x0ff0,0x0555)):
        mw(c,0x400000+2*(16+i))
    mw(0x0123,0x401ffe)
    image=Image.new('RGB',(320,224),(0,0,0));draw=ImageDraw.Draw(image)
    colors=[(0,0,0),(255,255,255),(0,255,190),(255,170,0),(100,100,255),(255,255,0),(80,80,80)]
    draw.rectangle((8,8,303,211),outline=colors[2],width=2)
    font=ImageFont.load_default()
    lines=[('H1 ARCADE',25,2),('NEO GEO / MVS',49,3),('68000 + Z80 + YM2610',75,1),
           ('DIRECTION + Z X C V',104,1),('SPACE COIN   ENTER START',125,1),
           ('P PAUSE  F SKIP  G VIEW',150,2),('H MUTE   ESC EXIT',168,2),
           ('ORIGINAL TEST / NO BIOS NEEDED',193,3)]
    for text,y,col in lines:
        box=draw.textbbox((0,0),text,font=font);w=box[2]-box[0]
        draw.text(((320-w)//2,y),text,font=font,fill=colors[col])
    palette=Image.new('P',(1,1));palette.putpalette([v for c in colors for v in c]+[0]*(768-len(colors)*3))
    image=image.quantize(palette=palette,dither=Image.Dither.NONE)
    fix=bytearray(0x20000);usage=bytearray(4096)
    for ty in range(1,28):
        for tx in range(1,39):
            tile=1+(ty-1)*38+(tx-1)
            for y in range(8):
                bits=0
                for x in range(8):bits|=image.getpixel((tx*8+x,ty*8+y))<<(x*4)
                struct.pack_into('<I',fix,tile*32+y*4,bits)
            usage[tile]=int(any(fix[tile*32:tile*32+32]))
            mw(0x7000+tx*32+ty,0x3c0000);mw(0x1000|tile,0x3c0002)
    # One visible sprite exercises file paging and the sprite renderer.
    def vr(address,value):mw(address,0x3c0000);mw(value,0x3c0002)
    vr(0,1);vr(1,0x0100);vr(0x8000,0x0fff);vr(0x8200,((512-180)<<7)|1);vr(0x8400,150<<7)
    if large:
        for col,tile,attr,x in ((1,32769,0x0100,170),(2,1,0x0110,190)):
            vr(col*64,tile);vr(col*64+1,attr);vr(0x8000+col,0x0fff)
            vr(0x8200+col,((512-180)<<7)|1);vr(0x8400+col,x<<7)
    word(0x46fc);word(0x2000) # move.w #$2000,sr
    word(0x4e72);word(0x2000);word(0x60fa) # stop; bra stop
    main[0x100:0x100+len(code)]=code
    code.clear()
    word(0x5279);long(0x100000) # addq.w #1, frame counter
    word(0x1039);long(0x300000) # read active-low controller
    word(0x13c0);long(0x100002)
    mb(0,0x300001) # watchdog
    word(0x4e73) # rte
    main[0x8000:0x8000+len(code)]=code
    z80=bytearray(0x20000);sound=bytearray()
    # PSG tone A, with the YM's FM/ADPCM channels initially silent.
    for address,value in ((0,120),(1,1),(7,0x3e),(8,12)):
        sound.extend(bytes((0x3e,address,0xd3,4,0x3e,value,0xd3,5)))
    sound.extend(bytes((0xfb,0x76,0x18,0xfd)))
    z80[:len(sound)]=sound;z80[0x38]=0xc9;z80[0x66]=0xed;z80[0x67]=0x45
    sprites=bytearray(4096)
    for y in range(16):
        pixels=[2 if x in (0,15) or y in (0,15) else 3 for x in range(16)]
        for half in range(2):
            group=pixels[half*8:(half+1)*8]
            struct.pack_into('<I',sprites,128+y*8+half*4,sum(p<<(28-x*4) for x,p in enumerate(group)))
    for data in (main,bios):data[0::2],data[1::2]=data[1::2],data[0::2]
    if large:
        pattern=sprites[128:256];sprites.extend(bytes(40*1024*1024-len(sprites)))
        for offset in (0x400080,0x800080):sprites[offset:offset+128]=pattern
        main.extend(bytes(4*1024*1024))
    return {1:z80,3:bytearray(12*1024*1024 if large else 256),5:bytearray(fix),6:fix,7:bios,8:main,9:sprites,
            10:bytearray(len(sprites)//512),11:usage,12:bytearray(0x20000),13:bytearray(usage)}

def main():
    output=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'build/test-rom/H1ARCADE-TEST.h1neo'
    if output.exists():
        # Generated fixture only; never overwrite an arbitrary game container.
        if output.name!='H1ARCADE-TEST.h1neo':raise FileExistsError(output)
        output.unlink()
    print(write_package(output,'h1test',make_regions()))
if __name__=='__main__':main()
