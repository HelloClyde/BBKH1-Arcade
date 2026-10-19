"""Patch a build copy of the pinned A68K generator with a RAM-only fast path."""
from pathlib import Path

HELPER=r'''
/* H1 local change: only plain work RAM, same byte lanes and word order.
 * AT and T9 are reserved scratch in upstream Memory_Read/Write. No MMIO,
 * ROM bank writes, IRQ handling, guest cycle accounting or fetch changes. */
static unsigned h1_bus_label;
#if H1_GEN_FAST_ROM
static unsigned h1_scheduled_read(char size,int address);
#endif
static unsigned h1_fast_memory(char size,int address,int data,int write){
 if(address==AT||address==T9||(write&&(data==AT||data==T9||PreDecLongMove)))return 0;
#if H1_GEN_FAST_ROM
 if(!write)return h1_scheduled_read(size,address);
#endif
 unsigned label=++h1_bus_label;const char *a=regnameslong[address];
 fprintf(fp,"\t.set noat\n");
 fprintf(fp,"\tsrl $t9,%s,20\n\tandi $t9,$t9,15\n\taddiu $t9,$t9,-1\n",a);
 fprintf(fp,"\tbne $t9,$zero,.Lh1_slow%u\n\tnop\n",label);
 if(size!='B'){
  fprintf(fp,"\tandi $at,%s,1\n",a);
  fprintf(fp,"\tbne $at,$zero,.Lh1_slow%u\n\tnop\n",label);
 }
 fprintf(fp,"\tandi $t9,%s,65535\n",a);
 if(size=='L'){
  /* Explicit 65532 limit: avoid mirroring or crossing into a different bus region. */
  fprintf(fp,"\tlui $at,0\n\tori $at,$at,65532\n\tsltu $at,$at,$t9\n");
  fprintf(fp,"\tbne $at,$zero,.Lh1_slow%u\n\tnop\n",label);
 }
 fprintf(fp,"\tlui $at,%%hi(h1_bus_ram)\n\tlw $at,%%lo(h1_bus_ram)($at)\n");
 fprintf(fp,"\tbeq $at,$zero,.Lh1_slow%u\n\tnop\n\taddu $at,$at,$t9\n",label);
 if(write){
  const char *d=regnameslong[data];
  if(size=='B')fprintf(fp,"\txori $at,$at,1\n\tsb %s,0($at)\n",d);
  else if(size=='W')fprintf(fp,"\tsh %s,0($at)\n",d);
  else fprintf(fp,"\tsrl $t9,%s,16\n\tsh $t9,0($at)\n\tsh %s,2($at)\n",d,d);
 }else{
  if(size=='B')fprintf(fp,"\txori $at,$at,1\n\tlbu $v0,0($at)\n");
  else if(size=='W')fprintf(fp,"\tlhu $v0,0($at)\n");
  else fprintf(fp,"\tlhu $v0,0($at)\n\tlhu $t9,2($at)\n\tsll $v0,$v0,16\n\tor $v0,$v0,$t9\n");
 }
 fprintf(fp,"\tb .Lh1_done%u\n\tnop\n.Lh1_slow%u:\n",label,label);
#if H1_GEN_FAST_ROM
 if(!write)h1_fast_rom(size,address,label);
#endif
 fprintf(fp,"\t.set at\n");
 return label;
}
static void h1_fast_end(unsigned label){if(label)fprintf(fp,".Lh1_done%u:\n",label);}
'''

VIDEO_HELPER=r'''
static unsigned h1_fast_video(char size,int address,int data){
 if(address==AT||address==T9||data==AT||data==T9||PreDecLongMove)return 0;
 unsigned label=++h1_bus_label;const char *a=regnameslong[address],*d=regnameslong[data];
 unsigned width=size=='B'?8:size=='W'?16:32;
 fprintf(fp,"\t.set noat\n\tmove $at,%s\n\tmove $t9,%s\n\tjal h1_bus_write%u\n\tnop\n",a,d,width);
 fprintf(fp,"\tbeqz $t9,.Lh1_vslow%u\n\tnop\n",label);
 fprintf(fp,"\tb .Lh1_vdone%u\n\tnop\n.Lh1_vslow%u:\n\t.set at\n",label,label);
 return label;
}
'''

ROM_HELPER=r'''
/* A shared leaf keeps the hot interpreter from growing by >100 KiB. */
static void h1_fast_rom(char size,int address,unsigned label){
 if(address==V0)return; /* V0 is the result/scratch register. */
 const char *a=regnameslong[address];unsigned width=size=='B'?1:size=='W'?2:4;
 fprintf(fp,"\tmove $at,%s\n\tjal h1_rom_read%u\n\tnop\n",a,width*8);
 fprintf(fp,"\tbeqz $t9,.Lh1_rslow%u\n\tnop\n\tb .Lh1_done%u\n\tnop\n.Lh1_rslow%u:\n",label,label,label);
}
'''

SCHEDULED_READ=r'''
/* Branch conditions are evaluated before their delay slot. Scratch work in
 * a failed guard never changes the original operand/address registers. */
static unsigned h1_scheduled_read(char size,int address){
 unsigned label=++h1_bus_label;const char *a=regnameslong[address];
 fprintf(fp,"\t.set noat\n\tsrl $t9,%s,20\n\tandi $t9,$t9,15\n\taddiu $t9,$t9,-1\n",a);
 fprintf(fp,"\tbne $t9,$zero,.Lh1_slow%u\n",label);
 if(size=='B')fprintf(fp,"\tandi $t9,%s,65535\n",a);
 else{
  fprintf(fp,"\tandi $at,%s,1\n\tbne $at,$zero,.Lh1_slow%u\n\tandi $t9,%s,65535\n",a,label,a);
 }
 if(size=='L'){
  fprintf(fp,"\tori $at,$zero,65532\n\tsltu $at,$at,$t9\n\tbne $at,$zero,.Lh1_slow%u\n",label);
 }
 /* Also fills the long-read boundary guard's delay slot. */
 fprintf(fp,"\tlui $at,%%hi(h1_bus_ram)\n\tlw $at,%%lo(h1_bus_ram)($at)\n");
 fprintf(fp,"\tbeq $at,$zero,.Lh1_slow%u\n\taddu $at,$at,$t9\n",label);
 if(size=='B'){
  fprintf(fp,"\txori $at,$at,1\n\tb .Lh1_done%u\n\tlbu $v0,0($at)\n",label);
 }else if(size=='W'){
  fprintf(fp,"\tb .Lh1_done%u\n\tlhu $v0,0($at)\n",label);
 }else{
  fprintf(fp,"\tlhu $v0,0($at)\n\tlhu $t9,2($at)\n\tsll $v0,$v0,16\n\tb .Lh1_done%u\n\tor $v0,$v0,$t9\n",label);
 }
 fprintf(fp,".Lh1_slow%u:\n",label);h1_fast_rom(size,address,label);
 fprintf(fp,"\t.set at\n");return label;
}
'''


def generate_copy(source:Path,target:Path,read_only=False,video=False,rom=False):
    original=source.read_text();read=original.index('void Memory_Read(');write=original.index('void Memory_Write(',read)
    end=original.index('/*\n * Fetch data from Code area',write)
    r=original[read:write];w=original[write:end]
    token='\tassert(AReg != T9);'
    assert r.count(token)==1
    r=r.replace(token,token+'\n\tunsigned h1_label=h1_fast_memory(Size,AReg,0,0);')
    token='\t/* ASG - changed these to call through the function pointers */'
    assert w.count(token)==1
    w=w.replace(token,('\tunsigned h1_label=0;\n\n' if read_only else '\tunsigned h1_label=h1_fast_memory(Size,AReg,DReg,1);\n\n')+token)
    if video:
        w=w.replace('\tunsigned h1_label=', '\tunsigned h1_video_label=h1_fast_video(Size,AReg,DReg);\n\tunsigned h1_label=',1)
    for name,body in (('read',r),('write',w)):
        position=body.rfind('\n}')
        assert position>0
        body=body[:position]+'\n\th1_fast_end(h1_label);'+body[position:]
        if name=='write' and video:
            position=body.rfind('\n}')
            body=body[:position]+'\n\tif(h1_video_label)fprintf(fp,".Lh1_vdone%u:\\n",h1_video_label);'+body[position:]
        if name=='read':r=body
        else:w=body
    target.write_text(original[:read]+('#define H1_GEN_FAST_ROM 1\n'+ROM_HELPER if rom else '')+HELPER+(SCHEDULED_READ if rom else '')+(VIDEO_HELPER if video else '')+'\n'+r+w+original[end:])
