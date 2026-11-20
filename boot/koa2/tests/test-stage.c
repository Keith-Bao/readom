/* SPDX-License-Identifier: GPL-2.0-only */
/* Runs boot/koa2/stage.c itself on the host, through tests/kshim, under ASan:
 * the identity gate, the open rules, begin/put/seal and every seal stage that
 * can fail, and that closing frees every page. Optional argument: a real
 * runtime DTB to stage in place of the fixture. */
#define _GNU_SOURCE 1   /* memmem */
#include "stage.c"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
extern u64 kshim_next;
extern long kshim_budget, kshim_live;
static unsigned char *zimage;
/* The module refuses user buffers above 4 GiB, as a 32-bit ARM process has
 * none; host heap pointers are 64-bit, so puts go through a low mapping. */
static unsigned char *bounce;
static unsigned char fdt_fixture[2048];
static unsigned char initramfs[300000];
static void w32(unsigned char *p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
/* Root (Zelda), /memory reg = base+size, /chosen with a bootargs, and one
 * optional header reservation. */
static size_t make_fdt(unsigned char *p, uint32_t base, uint32_t size, uint64_t rs, uint64_t rn)
{
    static const char ids[]="fsl,imx7d-zelda\0fsl,imx7d";
    static const char names[]="compatible\0reg\0bootargs";
    size_t so=72,c=so,no;
    memset(p,0,sizeof(fdt_fixture));
    w32(p,0xd00dfeed); w32(p+8,(uint32_t)so); w32(p+16,40); w32(p+20,17); w32(p+24,16);
    if(rn) { w32(p+40,(uint32_t)(rs>>32)); w32(p+44,(uint32_t)rs); w32(p+52,(uint32_t)rn); }
    w32(p+c,1); c+=8;
    w32(p+c,3); w32(p+c+4,sizeof(ids)); w32(p+c+8,0); c+=12; memcpy(p+c,ids,sizeof(ids)); c+=(sizeof(ids)+3)&~3u;
    w32(p+c,1); memcpy(p+c+4,"memory",7); c+=12;
    w32(p+c,3); w32(p+c+4,8); w32(p+c+8,11); w32(p+c+12,base); w32(p+c+16,size); c+=20;
    w32(p+c,2); c+=4;
    w32(p+c,1); memcpy(p+c+4,"chosen",7); c+=12;
    w32(p+c,3); w32(p+c+4,4); w32(p+c+8,15); memcpy(p+c+12,"old",4); c+=16;
    w32(p+c,2); c+=4; w32(p+c,2); c+=4; w32(p+c,9); c+=4;
    no=c; memcpy(p+no,names,sizeof(names));
    w32(p+12,(uint32_t)no); w32(p+32,sizeof(names)); w32(p+36,(uint32_t)(no-so)); w32(p+4,(uint32_t)(no+sizeof(names)));
    return no+sizeof(names);
}
static void hash(const unsigned char *d, size_t n, k2s_u8 *out)
{ SHA256_CTX c; sha256_init(&c); sha256_update(&c,d,n); sha256_final(&c,out); }
static struct file f;
static struct inode ino;
static long call(unsigned int cmd, void *arg) { return stage_ioctl(&f,cmd,(unsigned long)arg); }
static int opened_ok(void) { f.f_mode=FMODE_WRITE; f.private_data=NULL; return stage_open(&ino,&f); }
static void closed(void) { assert(stage_release(&ino,&f)==0); assert(kshim_live==0); }
static struct k2s_begin begin_for(const unsigned char *fdt, size_t fbytes, const unsigned char *rd, size_t rbytes)
{
    struct k2s_begin b;
    memset(&b,0,sizeof(b)); b.version=K2S_VERSION;
    b.images[0].bytes=K2S_KERNEL_BYTES; hash(zimage,K2S_KERNEL_BYTES,b.images[0].sha256);
    b.images[1].bytes=(k2s_u32)fbytes; hash(fdt,fbytes,b.images[1].sha256);
    b.images[2].bytes=(k2s_u32)rbytes; hash(rd,rbytes,b.images[2].sha256);
    return b;
}
static int put_all(unsigned int slot, const unsigned char *d, size_t n)
{
    size_t at=0;
    while(at<n) {
        struct k2s_put p={slot,(k2s_u32)at,(k2s_u32)(n-at>MAX_WRITE?MAX_WRITE:n-at),0,(k2s_u64)(uintptr_t)bounce};
        memcpy(bounce,d+at,p.bytes);
        if(call(K2S_PUT,&p)) return -1;
        at+=p.bytes;
    }
    return 0;
}
static struct k2s_status status(void) { struct k2s_status s; assert(call(K2S_STATUS,&s)==0); return s; }
/* Stages all three and seals; returns seal's result. */
static long run(const unsigned char *fdt, size_t fbytes, const unsigned char *rd, size_t rbytes)
{
    struct k2s_begin b=begin_for(fdt,fbytes,rd,rbytes);
    assert(call(K2S_BEGIN,&b)==0);
    assert(!put_all(0,zimage,K2S_KERNEL_BYTES) && !put_all(1,fdt,fbytes) && !put_all(2,rd,rbytes));
    return call(K2S_SEAL,NULL);
}
static void set_uts(const char *release, const char *version)
{
    memset(&kshim_uts,0,sizeof(kshim_uts));
    strcpy(kshim_uts.release,release); strcpy(kshim_uts.version,version); strcpy(kshim_uts.machine,"armv7l");
}
static void identity_gate(void)
{
    set_uts("4.1.15-lab126-koa2-headless-smoke",K2S_STOCK_VERSION); assert(kshim_init()==-ENOEXEC && !kshim_misc);
    set_uts(K2S_STOCK_RELEASE,"#2 SMP PREEMPT"); assert(kshim_init()==-ENOEXEC && !kshim_misc);
    set_uts(K2S_STOCK_RELEASE,K2S_STOCK_VERSION);
    kshim_phys_offset=0x60000000u; assert(kshim_init()==-ENOEXEC && !kshim_misc);
    kshim_phys_offset=0x80000000u; assert(kshim_init()==0 && kshim_misc==&device);
    assert(!strcmp(kshim_misc->name,"koa2-handoff-stage") && kshim_misc->mode==0600);
}
static void open_rules(void)
{
    struct file g; struct k2s_status st;
    kshim_capable=0; assert(opened_ok()==-EPERM); kshim_capable=1;
    f.f_mode=0; assert(stage_open(&ino,&f)==-EPERM);
    assert(opened_ok()==0);
    g.f_mode=FMODE_WRITE; assert(stage_open(&ino,&g)==-EBUSY);
    kshim_capable=0; assert(call(K2S_STATUS,&st)==-EPERM); kshim_capable=1;
    assert(call(0x1234,NULL)==-ENOTTY);
    assert(stage_ioctl(&f,K2S_SEAL,1)==-EINVAL && stage_ioctl(&f,K2S_DISCARD,1)==-EINVAL);
    closed();
}
/* The whole path: staged, checked, rewritten, planned; nothing to execute. */
static void happy_path(size_t n)
{
    struct k2s_status st; struct k2s_begin b;
    struct store *s; struct koa2_range r[4]; size_t c;
    unsigned char out[4096];
    assert(run(fdt_fixture,n,initramfs,sizeof(initramfs))==0);
    st=status();
    assert(st.state==K2S_SEALED && st.plan_valid==1 && st.execute_available==0 && st.seal_stage==0);
    /* The FDT slot now holds the rewrite: no memreserve, the build's bootargs. */
    s=f.private_data;
    assert(st.fdt_bytes<=sizeof(out)); slot_read(s,1,0,out,st.fdt_bytes);
    assert(koa2_fdt_reservations(out,st.fdt_bytes,r,4,&c)==0 && c==0);
    assert(s->plan.segments[0].dest==0x80d00000u && s->plan.segments[1].dest==0x81400000u &&
           s->plan.segments[2].dest==0x81500000u && s->plan.segments[2].size==sizeof(initramfs));
    assert(memmem(out,st.fdt_bytes,K2S_BOOTARGS,sizeof(K2S_BOOTARGS)));
    assert(!memmem(out,st.fdt_bytes,"old",4));
    memset(&b,0,sizeof(b));
    assert(call(K2S_SEAL,NULL)==-EPERM && call(K2S_BEGIN,&b)==-EBUSY);
    assert(call(K2S_DISCARD,NULL)==0 && kshim_live==0 && status().state==K2S_EMPTY);
}
static void begin_and_put_rules(size_t n)
{
    struct k2s_begin b; struct k2s_put p; struct k2s_status st;
    /* Wrong kernel: refused before a page is allocated. */
    b=begin_for(fdt_fixture,n,initramfs,sizeof(initramfs)); b.images[0].sha256[0]^=1;
    assert(call(K2S_BEGIN,&b)==-EKEYREJECTED && kshim_live==0);
    b=begin_for(fdt_fixture,n,initramfs,sizeof(initramfs)); b.images[0].bytes--;
    assert(call(K2S_BEGIN,&b)==-EKEYREJECTED && kshim_live==0);
    b=begin_for(fdt_fixture,n,initramfs,sizeof(initramfs)); b.version=2; assert(call(K2S_BEGIN,&b)==-EINVAL);
    b=begin_for(fdt_fixture,n,initramfs,sizeof(initramfs)); b.images[1].bytes=K2S_FDT_MAX+1; assert(call(K2S_BEGIN,&b)==-EINVAL);
    b=begin_for(fdt_fixture,n,initramfs,sizeof(initramfs)); b.images[2].bytes=0; assert(call(K2S_BEGIN,&b)==-EINVAL);
    b=begin_for(fdt_fixture,n,initramfs,sizeof(initramfs)); b.images[2].bytes=0x1a900000u+1; assert(call(K2S_BEGIN,&b)==-EINVAL);
    /* Out of order. */
    p=(struct k2s_put){0,0,16,0,(k2s_u64)(uintptr_t)zimage}; assert(call(K2S_PUT,&p)==-EPERM);
    assert(call(K2S_SEAL,NULL)==-EPERM);
    /* The allocator runs dry: everything given back. */
    kshim_budget=10; b=begin_for(fdt_fixture,n,initramfs,sizeof(initramfs));
    assert(call(K2S_BEGIN,&b)==-ENOMEM && kshim_live==0); kshim_budget=-1;
    /* Puts: not in sequence, too long, past the slot, no such slot, faulting buffer. */
    b=begin_for(fdt_fixture,n,initramfs,sizeof(initramfs)); assert(call(K2S_BEGIN,&b)==0);
    p=(struct k2s_put){0,16,16,0,(k2s_u64)(uintptr_t)zimage}; assert(call(K2S_PUT,&p)==-EINVAL);
    p=(struct k2s_put){0,0,MAX_WRITE+1,0,(k2s_u64)(uintptr_t)zimage}; assert(call(K2S_PUT,&p)==-EINVAL);
    p=(struct k2s_put){1,0,(k2s_u32)n+1,0,(k2s_u64)(uintptr_t)fdt_fixture}; assert(call(K2S_PUT,&p)==-EINVAL);
    p=(struct k2s_put){3,0,16,0,(k2s_u64)(uintptr_t)zimage}; assert(call(K2S_PUT,&p)==-EINVAL);
    p=(struct k2s_put){0,0,16,1,(k2s_u64)(uintptr_t)zimage}; assert(call(K2S_PUT,&p)==-EINVAL);
    p=(struct k2s_put){0,0,16,0,(k2s_u64)(uintptr_t)zimage}; assert(call(K2S_PUT,&p)==-EINVAL); /* above 4 GiB */
    p=(struct k2s_put){0,0,16,0,(k2s_u64)(uintptr_t)bounce}; kshim_fault_source=bounce;
    assert(call(K2S_PUT,&p)==-EFAULT && kshim_live==0 && status().state==K2S_EMPTY); kshim_fault_source=NULL;
    /* Seal before every byte is in. */
    b=begin_for(fdt_fixture,n,initramfs,sizeof(initramfs)); assert(call(K2S_BEGIN,&b)==0);
    assert(!put_all(0,zimage,K2S_KERNEL_BYTES));
    assert(call(K2S_SEAL,NULL)==-ENODATA); st=status();
    assert(st.state==K2S_EMPTY && st.seal_stage==K2S_STAGE_INCOMPLETE && kshim_live==0);
    /* Bytes that are not what was declared. */
    b=begin_for(fdt_fixture,n,initramfs,sizeof(initramfs)); assert(call(K2S_BEGIN,&b)==0);
    initramfs[100]^=1;
    assert(!put_all(0,zimage,K2S_KERNEL_BYTES) && !put_all(1,fdt_fixture,n) && !put_all(2,initramfs,sizeof(initramfs)));
    initramfs[100]^=1;
    assert(call(K2S_SEAL,NULL)==-EBADMSG); st=status();
    assert(st.seal_stage==K2S_STAGE_HASH && st.seal_result==2 && kshim_live==0);
}
static void seal_stages(void)
{
    struct k2s_status st; unsigned char *big; size_t n;
    /* Not a Zelda FDT; a reservation nothing measured covers; another /memory. */
    n=make_fdt(fdt_fixture,0x80000000u,0x20000000u,0x9d681000u,0xc000u);
    fdt_fixture[0]^=1; assert(run(fdt_fixture,n,initramfs,sizeof(initramfs))==-ENOEXEC); fdt_fixture[0]^=1;
    assert(status().seal_stage==K2S_STAGE_FDT && kshim_live==0);
    n=make_fdt(fdt_fixture,0x80000000u,0x20000000u,0x81000000u,0x1000u);
    assert(run(fdt_fixture,n,initramfs,sizeof(initramfs))==-ENOEXEC && status().seal_stage==K2S_STAGE_FDT_RESERVATIONS);
    n=make_fdt(fdt_fixture,0x80000000u,0x10000000u,0,0);
    assert(run(fdt_fixture,n,initramfs,sizeof(initramfs))==-ENOEXEC && status().seal_stage==K2S_STAGE_MEMORY);
    assert(kshim_live==0);
    /* A source page on a destination: an initramfs reaching past 0x88000000,
     * where the allocator is handing out pages. */
    n=make_fdt(fdt_fixture,0x80000000u,0x20000000u,0,0);
    big=calloc(1,0x6b01000u); assert(big);
    kshim_next=0x88000000u;
    assert(run(fdt_fixture,n,big,0x6b01000u)==-EADDRINUSE);
    st=status(); assert(st.seal_stage==K2S_STAGE_PAGES && st.seal_result==2 && kshim_live==0);
    free(big);
    /* Pages offered in the zImage window are held aside, not used, then freed. */
    kshim_next=0x87ff0000u;
    assert(run(fdt_fixture,n,initramfs,sizeof(initramfs))==0); st=status();
    assert(st.collision_pages==16 && st.plan_valid==1);
    assert(call(K2S_DISCARD,NULL)==0 && kshim_live==0);
    kshim_next=0x90000000u;
}
int main(int argc, char **argv)
{
    struct k2s_status st; size_t n,i;
    zimage=calloc(1,K2S_KERNEL_BYTES); assert(zimage);
    bounce=mmap(NULL,MAX_WRITE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    assert(bounce!=MAP_FAILED && (uintptr_t)bounce+MAX_WRITE<=0x100000000ull);
    for(i=0;i<sizeof(initramfs);++i) initramfs[i]=(unsigned char)(i*7);
    identity_gate();
    open_rules();
    assert(opened_ok()==0);
    n=make_fdt(fdt_fixture,0x80000000u,0x20000000u,0x9d681000u,0xc000u);
    happy_path(n);
    begin_and_put_rules(n);
    seal_stages();
    closed();
    if(argc==2) {
        FILE *in=fopen(argv[1],"rb"); long bytes; unsigned char *real;
        assert(in && !fseek(in,0,SEEK_END)); bytes=ftell(in); assert(bytes>0 && bytes<=(long)K2S_FDT_MAX);
        rewind(in); real=malloc((size_t)bytes); assert(real && fread(real,1,(size_t)bytes,in)==(size_t)bytes); fclose(in);
        assert(opened_ok()==0);
        assert(run(real,(size_t)bytes,initramfs,sizeof(initramfs))==0); st=status();
        assert(st.plan_valid==1 && st.execute_available==0);
        printf("real FDT %ld bytes staged and rewritten to %u\n",bytes,st.fdt_bytes);
        closed(); free(real);
    }
    kshim_exit(); assert(!kshim_misc);
    free(zimage);
    puts("KOA2 stage module: identity gate, open rules, begin/put/seal, every seal stage, page ownership and release passed");
    return 0;
}
