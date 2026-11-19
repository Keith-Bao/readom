/* SPDX-License-Identifier: GPL-2.0-only */
#include "fdt-rewrite.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* A tiny FDT builder: tokens into s[], names into n[]. */
struct b { unsigned char s[1024], n[256]; size_t sl, nl; };
static void w32(unsigned char *p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static uint32_t r32(const unsigned char *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void t(struct b *b, uint32_t v) { w32(b->s+b->sl,v); b->sl+=4; }
static void raw(struct b *b, const void *d, size_t n) { memcpy(b->s+b->sl,d,n); b->sl+=n; while(b->sl&3) b->s[b->sl++]=0; }
static uint32_t name(struct b *b, const char *k) {
    size_t i,len=strlen(k)+1;
    for(i=0;i+len<=b->nl;i++) if(!memcmp(b->n+i,k,len)) return (uint32_t)i;
    memcpy(b->n+b->nl,k,len); b->nl+=len; return (uint32_t)(b->nl-len);
}
static void node(struct b *b, const char *k) { t(b,1); raw(b,k,strlen(k)+1); }
static void prop(struct b *b, const char *k, const void *d, size_t n) { t(b,3); t(b,(uint32_t)n); t(b,name(b,k)); raw(b,d,n); }
static void cell(struct b *b, const char *k, uint32_t v) { unsigned char c[4]; w32(c,v); prop(b,k,c,4); }
static size_t finish(struct b *b, unsigned char *p, uint64_t rs, uint64_t rn) {
    size_t so=40+32,no;
    memset(p,0,2048);
    w32(p,0xd00dfeed); w32(p+8,(uint32_t)so); w32(p+16,40); w32(p+20,17); w32(p+24,16);
    if(rn) { w32(p+40,(uint32_t)(rs>>32)); w32(p+44,(uint32_t)rs); w32(p+52,(uint32_t)rn); }
    memcpy(p+so,b->s,b->sl); no=so+b->sl; memcpy(p+no,b->n,b->nl);
    w32(p+12,(uint32_t)no); w32(p+32,(uint32_t)b->nl); w32(p+36,(uint32_t)b->sl); w32(p+4,(uint32_t)(no+b->nl));
    return no+b->nl;
}
static const char ids[]="fsl,imx7d-zelda\0fsl,imx7d";
static size_t stock_like(unsigned char *p, int with_chosen) {
    static struct b b; unsigned char mem[8];
    memset(&b,0,sizeof(b));
    node(&b,""); cell(&b,"#address-cells",1); prop(&b,"compatible",ids,sizeof(ids));
    if(with_chosen) {
        node(&b,"chosen"); cell(&b,"linux,initrd-end",0x9dbeba00u); cell(&b,"linux,initrd-start",0x9d691000u);
        t(&b,4); prop(&b,"bootargs","root=/dev/mmcblk2p5",20); prop(&b,"stdout-path","serial0",8);
        node(&b,"sub"); prop(&b,"bootargs","kept",5); t(&b,2);
        t(&b,2);
    }
    w32(mem,0x80000000u); w32(mem+4,0x20000000u);
    node(&b,"memory"); prop(&b,"device_type","memory",7); prop(&b,"reg",mem,8); t(&b,2);
    node(&b,"chosen-not"); prop(&b,"bootargs","kept too",9); t(&b,2);
    t(&b,2); t(&b,9);
    return finish(&b,p,0x9d681000u,0xc000u);
}
/* Value of property key in the node at path depth 1 named node ("" = root), or NULL. */
static const unsigned char *get(const unsigned char *p, const char *nodename, const char *key, uint32_t *len, int *count) {
    size_t so=r32(p+8),no=r32(p+12),c=so,end=so+r32(p+36);
    const unsigned char *found=NULL; unsigned depth=0; int here=0;
    *count=0;
    while(c<end) {
        uint32_t tok=r32(p+c); c+=4;
        if(tok==1) { const char *n=(const char *)p+c; depth++;
            here=(depth==1 && !*nodename) || (depth==2 && !strcmp(n,nodename));
            c+=(strlen(n)+4)&~(size_t)3; }
        else if(tok==2) { depth--; here=depth==1 && !*nodename; }
        else if(tok==3) { uint32_t l=r32(p+c),k=r32(p+c+4);
            if(here && !strcmp((const char *)p+no+k,key)) { found=p+c+8; *len=l; ++*count; }
            c+=8+((l+3)&~(size_t)3); }
        else if(tok==9) break;
    }
    return found;
}
static const char args[]="console=ttymxc0,115200 rdinit=/init koa2_smoke_hold=20";
static const struct koa2_range keep[]={{0x81400000u,0xd000u}};
static struct koa2_fdt_edit edit(void) {
    struct koa2_fdt_edit e={args,0x81500000u,0x81500000u+271689u,keep,1};
    return e;
}
static unsigned char in[2048], out[4096], again[4096], small[4096];
static void check_rewritten(const unsigned char *o, size_t n, int initrd) {
    struct koa2_fdt_info info; struct koa2_range r[4]; size_t c; uint32_t len; int count;
    const unsigned char *v;
    assert(koa2_fdt_validate(o,n,&info)==0);
    assert(koa2_fdt_reservations(o,n,r,4,&c)==0 && c==1 && r[0].start==0x81400000u && r[0].size==0xd000u);
    v=get(o,"chosen","bootargs",&len,&count); assert(v && count==1 && len==sizeof(args) && !memcmp(v,args,len));
    v=get(o,"chosen","linux,initrd-start",&len,&count);
    if(initrd) { assert(v && count==1 && len==4 && r32(v)==0x81500000u); }
    else assert(!v);
    v=get(o,"chosen","linux,initrd-end",&len,&count);
    if(initrd) { assert(v && count==1 && len==4 && r32(v)==0x81500000u+271689u); }
    else assert(!v);
    v=get(o,"chosen-not","bootargs",&len,&count); assert(v && count==1 && !strcmp((const char *)v,"kept too"));
    v=get(o,"memory","reg",&len,&count); assert(v && len==8 && r32(v)==0x80000000u);
}
int main(int argc, char **argv) {
    struct koa2_fdt_edit e=edit(),bad;
    struct koa2_fdt_seen seen;
    size_t n=stock_like(in,1),m,m2,i;
    uint32_t len,state=7; int count;
    char long_args[KOA2_BOOTARGS_MAX+2];
    struct koa2_range broken={UINT64_MAX,2};
    /* The stock shape: initrd and bootargs replaced, everything else kept. */
    assert(koa2_fdt_rewrite(in,n,&e,out,sizeof(out),&m,&seen)==0);
    check_rewritten(out,m,1);
    assert(seen.chosen_nodes==1 && seen.memory_regs==1 && seen.memory_start==0x80000000u && seen.memory_size==0x20000000u);
    assert(get(out,"chosen","stdout-path",&len,&count) && count==1);
    /* A second pass changes nothing: the names now exist in the strings block. */
    assert(koa2_fdt_rewrite(out,m,&e,again,sizeof(again),&m2,&seen)==0 && m2==m && !memcmp(out,again,m));
    /* No initramfs: the stock initrd properties go and none are written. */
    e.initrd_start=e.initrd_end=0;
    assert(koa2_fdt_rewrite(in,n,&e,out,sizeof(out),&m,&seen)==0);
    check_rewritten(out,m,0);
    /* No /chosen at all: one is added. */
    e=edit(); n=stock_like(in,0);
    assert(koa2_fdt_rewrite(in,n,&e,out,sizeof(out),&m,&seen)==0 && seen.chosen_nodes==0);
    check_rewritten(out,m,1);
    /* Every short buffer fails cleanly and leaves no partial FDT behind. */
    n=stock_like(in,1);
    assert(koa2_fdt_rewrite(in,n,&e,out,sizeof(out),&m,&seen)==0);
    for(i=0;i<m;i++) {
        size_t got=1;
        memset(small,0xa5,sizeof(small));
        assert(koa2_fdt_rewrite(in,n,&e,small,i,&got,&seen)==KOA2_BOUNDS && got==0);
        assert(i<4 || r32(small)!=0xd00dfeed);
    }
    /* Edits out of range. */
    bad=e; bad.bootargs=NULL; assert(koa2_fdt_rewrite(in,n,&bad,out,sizeof(out),&m,&seen)==KOA2_RANGE && m==0);
    memset(long_args,'a',sizeof(long_args)-1); long_args[sizeof(long_args)-1]=0;
    bad=e; bad.bootargs=long_args; assert(koa2_fdt_rewrite(in,n,&bad,out,sizeof(out),&m,&seen)==KOA2_RANGE);
    long_args[KOA2_BOOTARGS_MAX]=0; assert(koa2_fdt_rewrite(in,n,&bad,out,sizeof(out),&m,&seen)==0);
    bad=e; bad.initrd_end=bad.initrd_start; assert(koa2_fdt_rewrite(in,n,&bad,out,sizeof(out),&m,&seen)==KOA2_RANGE);
    bad=e; bad.initrd_start=0; bad.initrd_end=0x1000; assert(koa2_fdt_rewrite(in,n,&bad,out,sizeof(out),&m,&seen)==0);
    bad=e; bad.initrd_end=0x100000000ull; assert(koa2_fdt_rewrite(in,n,&bad,out,sizeof(out),&m,&seen)==KOA2_RANGE);
    bad=e; bad.reserve=&broken; assert(koa2_fdt_rewrite(in,n,&bad,out,sizeof(out),&m,&seen)==KOA2_RANGE);
    bad=e; bad.reserve=NULL; assert(koa2_fdt_rewrite(in,n,&bad,out,sizeof(out),&m,&seen)==KOA2_RANGE);
    bad=e; bad.reserve_count=0; bad.reserve=NULL; assert(koa2_fdt_rewrite(in,n,&bad,out,sizeof(out),&m,&seen)==0);
    assert(koa2_fdt_rewrite(in,n,NULL,out,sizeof(out),&m,&seen)==KOA2_RANGE);
    assert(koa2_fdt_rewrite(in,n,&e,NULL,sizeof(out),&m,&seen)==KOA2_BOUNDS);
    assert(koa2_fdt_rewrite(in,n,&e,out,sizeof(out),NULL,&seen)==KOA2_BOUNDS);
    assert(koa2_fdt_rewrite(in,n,&e,out,sizeof(out),&m,NULL)==KOA2_BOUNDS);
    assert(koa2_fdt_rewrite(in,n-1,&e,out,sizeof(out),&m,&seen)!=0 && m==0);
    /* Damaged input: refused, or rewritten into something that validates. */
    for(i=0;i<20000;i++) {
        static unsigned char mut[2048]; unsigned j; struct koa2_fdt_info info;
        memcpy(mut,in,n);
        for(j=0;j<3;j++) { state=state*1664525u+1013904223u; mut[state%n]^=(unsigned char)(state>>24); }
        if(koa2_fdt_rewrite(mut,n,&e,out,sizeof(out),&m,&seen)==0) assert(koa2_fdt_validate(out,m,&info)==0);
        else assert(m==0);
    }
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb"); long bytes; unsigned char *p,*o;
        assert(f && !fseek(f,0,SEEK_END)); bytes=ftell(f); assert(bytes>0 && bytes<16*1024*1024);
        rewind(f); p=malloc((size_t)bytes); o=malloc((size_t)bytes+4096);
        assert(p && o && fread(p,1,(size_t)bytes,f)==(size_t)bytes); fclose(f);
        assert(koa2_fdt_rewrite(p,(size_t)bytes,&e,o,(size_t)bytes+4096,&m,&seen)==0);
        assert(seen.memory_regs==1 && seen.memory_start==0x80000000u && seen.memory_size==0x20000000u);
        printf("rewrote %ld bytes into %zu\n",bytes,m);
        free(p); free(o);
    }
    puts("KOA2 FDT rewrite: reserve map, chosen replace/add, idempotence, short buffers, bad edits and 20000 mutations passed");
    return 0;
}
