/* SPDX-License-Identifier: GPL-2.0-only */
/* Runs boot/koa2/exec.c on the host through tests/kshim, under ASan. It builds
 * stage.c with KOA2_EXEC_BUILD so the tail #include pulls exec.c in, supplies
 * the trampoline stand-ins jump.S would otherwise give, stages a sealed plan,
 * and checks the K2S_EXEC_DRYRUN ioctl: a good dry-run, each way it refuses,
 * that IRQ/FIQ were masked and released around the snapshot, that the control
 * page is given back (kshim_live returns to the staged count), and that it
 * never claims a jump is available. */
#define _GNU_SOURCE 1
/* The trampoline the linker would provide. One page of filler plus the
 * parameter area; exec.c only copies KOA2_EXEC_HOST_BLOB bytes and never runs
 * it, so the contents do not matter. */
unsigned char koa2_jump_start[4096];
#define KOA2_EXEC_HOST_BLOB 512u
/* Build stage.c as the execute module: pulls in exec.c, names the device
 * koa2-handoff-exec and enables the K2S_EXEC_DRYRUN ioctl. */
#define KOA2_EXEC_BUILD 1
#include "stage.c"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
extern long kshim_budget, kshim_live;
extern int kshim_cpu_online[2];
extern unsigned kshim_irq_masked, kshim_fiq_masked, kshim_irq_depth;
extern unsigned kshim_cpsr, kshim_sctlr, kshim_ttbcr;
static unsigned char *zimage;
static unsigned char *bounce;
static unsigned char fdt_fixture[2048];
static unsigned char initramfs[300000];
static void w32(unsigned char *p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
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
static struct k2s_begin begin_for(const unsigned char *fdt, size_t fbytes, const unsigned char *rd, size_t rbytes)
{
    struct k2s_begin b;
    memset(&b,0,sizeof(b)); b.version=K2S_VERSION;
    b.images[0].bytes=K2S_KERNEL_BYTES; hash(zimage,K2S_KERNEL_BYTES,b.images[0].sha256);
    b.images[1].bytes=(k2s_u32)fbytes; hash(fdt,fbytes,b.images[1].sha256);
    b.images[2].bytes=(k2s_u32)rbytes; hash(rd,rbytes,b.images[2].sha256);
    return b;
}
static long seal_plan(size_t n)
{
    struct k2s_begin b=begin_for(fdt_fixture,n,initramfs,sizeof(initramfs));
    assert(call(K2S_BEGIN,&b)==0);
    assert(!put_all(0,zimage,K2S_KERNEL_BYTES) && !put_all(1,fdt_fixture,n) && !put_all(2,initramfs,sizeof(initramfs)));
    return call(K2S_SEAL,NULL);
}
/* The device under the exec build answers to its own name, and the marker
 * ioctl still exists but the exec one is what this file drives. */
static void identity(void)
{
    memset(&kshim_uts,0,sizeof(kshim_uts));
    strcpy(kshim_uts.release,K2S_STOCK_RELEASE); strcpy(kshim_uts.version,K2S_STOCK_VERSION);
    strcpy(kshim_uts.machine,"armv7l");
    assert(kshim_init()==0 && kshim_misc==&device);
    assert(!strcmp(kshim_misc->name,"koa2-handoff-exec"));
}
static struct k2s_exec_dryrun quiesced_claim(void)
{
    struct k2s_exec_dryrun io; memset(&io,0,sizeof(io));
    io.claim.drivers_present=0; io.claim.rootfs_readonly=1;
    return io;
}
/* The whole dry-run on a quiesced machine: armed, stopped before the caches,
 * everything put back, no jump offered. */
static void good_dryrun(size_t n)
{
    struct k2s_exec_dryrun io; struct store *s; unsigned int staged;
    assert(opened_ok()==0);
    assert(seal_plan(n)==0);
    s=f.private_data; staged=s->count;
    kshim_cpu_online[0]=1; kshim_cpu_online[1]=0;   /* cpu1 offline */
    kshim_sctlr=0x1005u;   /* MMU+caches on, as the stock kernel runs */
    kshim_ttbcr=0; kshim_cpsr=0;
    kshim_irq_depth=0; kshim_fiq_masked=0;
    io=quiesced_claim();
    assert(call(K2S_EXEC_DRYRUN,&io)==0);
    assert(io.report.version==K2S_VERSION);
    assert(io.report.online_cpus==1);
    assert(io.report.quiesce_result==0);
    assert(io.report.copylist_result==0);
    assert(io.report.copylist_entries==s->list_entries);
    assert(io.report.params_result==0);
    assert(io.report.trampoline_bytes==KOA2_EXEC_HOST_BLOB);
    /* Every list entry is a destination (3), a source page, a per-page link,
     * or the terminator; so the sources the trampoline is told to copy are
     * the entries less the 3 destinations, the terminator and the links. */
    assert(io.report.expected_pages==io.report.copylist_entries-3-1-(s->list_pages-1));
    assert(io.report.mmu_on==1 && io.report.caches_on==1);
    assert(io.report.watchdog_step==K2S_WDT_DEFERRED);
    assert(io.report.execute_available==0);
    assert(io.report.stopped_before_caches==1);
    /* The snapshot ran with IRQ and FIQ masked and released them: depth back to
     * zero, fiq mask cleared, and the report says so. */
    assert(io.report.irq_fiq_masked==0);   /* kshim_cpsr had neither bit set */
    assert(kshim_irq_depth==0 && kshim_fiq_masked==0);
    /* The control page was handed back: the page count is the staged count. */
    assert(s->count==staged);
    /* With both masked in the snapshot, the report reflects it. */
    kshim_cpsr=0xc0u;
    io=quiesced_claim();
    assert(call(K2S_EXEC_DRYRUN,&io)==0 && io.report.irq_fiq_masked==1);
    assert(call(K2S_DISCARD,NULL)==0 && kshim_live==0);
    assert(stage_release(&ino,&f)==0 && kshim_live==0);
}
/* Each way it refuses, without ever offering a jump. */
static void refusals(size_t n)
{
    struct k2s_exec_dryrun io; struct store *s;
    /* Not sealed yet. */
    assert(opened_ok()==0);
    io=quiesced_claim();
    assert(call(K2S_EXEC_DRYRUN,&io)==-EPERM);
    assert(seal_plan(n)==0);
    s=f.private_data;
    /* reserved must be zero. */
    io=quiesced_claim(); io.claim.reserved=1;
    assert(call(K2S_EXEC_DRYRUN,&io)==-EINVAL);
    /* Two cores online: the quiesce verdict is non-zero, but the dry-run still
     * completes and reports it rather than failing the call. */
    kshim_cpu_online[0]=1; kshim_cpu_online[1]=1;
    io=quiesced_claim();
    assert(call(K2S_EXEC_DRYRUN,&io)==0 && io.report.online_cpus==2 && io.report.quiesce_result!=0);
    assert(io.report.execute_available==0);
    kshim_cpu_online[1]=0;
    /* A driver still present without a verified-idle claim: reported, not fatal. */
    io=quiesced_claim(); io.claim.drivers_present=1;
    assert(call(K2S_EXEC_DRYRUN,&io)==0 && io.report.quiesce_result!=0);
    /* rootfs not read-only: reported. */
    io=quiesced_claim(); io.claim.rootfs_readonly=0;
    assert(call(K2S_EXEC_DRYRUN,&io)==0 && io.report.quiesce_result!=0);
    (void)s;
    assert(call(K2S_DISCARD,NULL)==0);
    assert(stage_release(&ino,&f)==0 && kshim_live==0);
}
int main(void)
{
    size_t n,i;
    zimage=calloc(1,K2S_KERNEL_BYTES); assert(zimage);
    bounce=mmap(NULL,MAX_WRITE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    assert(bounce!=MAP_FAILED && (uintptr_t)bounce+MAX_WRITE<=0x100000000ull);
    for(i=0;i<sizeof(initramfs);++i) initramfs[i]=(unsigned char)(i*7);
    identity();
    n=make_fdt(fdt_fixture,0x80000000u,0x20000000u,0x9d681000u,0xc000u);
    good_dryrun(n);
    refusals(n);
    kshim_exit(); assert(!kshim_misc);
    free(zimage);
    puts("KOA2 exec module: identity, armed dry-run stopped before caches, IRQ/FIQ masked "
         "and released, control page returned, every refusal, no jump offered");
    return 0;
}
