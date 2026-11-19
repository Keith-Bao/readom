/* SPDX-License-Identifier: GPL-2.0-only */
#include "contract.h"
#include <string.h>
#define MAX_DTB (16u * 1024u * 1024u)
static uint32_t be32(const unsigned char *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static uint64_t be64(const unsigned char *p) { return ((uint64_t)be32(p)<<32)|be32(p+4); }
static int span(size_t start, size_t count, size_t limit) { return start<=limit && count<=limit-start; }
static int intersect(size_t a, size_t n, size_t b, size_t m) { return a<b+m && b<a+n; }
static int strings(const unsigned char *p, size_t bytes, const char *wanted) {
    int found=0;
    while(bytes) {
        const unsigned char *end=memchr(p,0,bytes);
        size_t n;
        if(!end) return -1;
        n=(size_t)(end-p);
        if(n==strlen(wanted) && !memcmp(p,wanted,n)) found=1;
        p+=n+1; bytes-=n+1;
    }
    return found;
}
int koa2_fdt_validate(const void *input, size_t bytes, struct koa2_fdt_info *info) {
    const unsigned char *p=input;
    struct koa2_fdt_info result={0,0,0,0};
    size_t so,ss,no,ns,ro,re,cursor,end;
    unsigned depth=0;
    int root=0,closed=0,compatible=0;
    if(info) memset(info,0,sizeof(*info));
    if(!p || !info || bytes<40 || bytes>MAX_DTB) return KOA2_BOUNDS;
    if(be32(p)!=0xd00dfeed || be32(p+4)!=bytes || be32(p+20)!=17 || be32(p+24)>17) return KOA2_FORMAT;
    so=be32(p+8); no=be32(p+12); ro=be32(p+16); ns=be32(p+32); ss=be32(p+36);
    if(so<40 || no<40 || ro<40 || (so&3) || (ro&7) || (ss&3) ||
       !span(so,ss,bytes) || !span(no,ns,bytes) || intersect(so,ss,no,ns)) return KOA2_BOUNDS;
    re=ro;
    for(;;) {
        uint64_t address,length;
        if(!span(re,16,bytes) || intersect(re,16,so,ss) || intersect(re,16,no,ns)) return KOA2_BOUNDS;
        address=be64(p+re); length=be64(p+re+8); re+=16;
        if(!address && !length) break;
        if(!length || address>UINT64_MAX-length) return KOA2_RANGE;
        if(++result.reservations>1024) return KOA2_FORMAT;
    }
    cursor=so; end=so+ss;
    while(span(cursor,4,end)) {
        uint32_t token=be32(p+cursor); cursor+=4;
        if(token==1) {
            const unsigned char *zero;
            size_t len,padded;
            if(closed || depth>=64) return KOA2_FORMAT;
            zero=memchr(p+cursor,0,end-cursor);
            if(!zero) return KOA2_BOUNDS;
            len=(size_t)(zero-(p+cursor));
            if((!root && len) || (root && !len)) return KOA2_FORMAT;
            padded=(len+1+3)&~(size_t)3;
            if(!span(cursor,padded,end)) return KOA2_BOUNDS;
            cursor+=padded; root=1; depth++; result.nodes++;
        } else if(token==2) {
            if(!depth) return KOA2_FORMAT;
            if(!--depth) closed=1;
        } else if(token==3) {
            uint32_t length,name;
            size_t padded;
            const char *key;
            if(!depth || !span(cursor,8,end)) return KOA2_FORMAT;
            length=be32(p+cursor); name=be32(p+cursor+4); cursor+=8;
            if(name>=ns || !memchr(p+no+name,0,ns-name)) return KOA2_BOUNDS;
            padded=((size_t)length+3)&~(size_t)3;
            if(padded<length || !span(cursor,padded,end)) return KOA2_BOUNDS;
            key=(const char *)(p+no+name);
            if(depth==1 && !strcmp(key,"compatible")) {
                int zelda,soc;
                if(compatible) return KOA2_FORMAT;
                zelda=strings(p+cursor,length,"fsl,imx7d-zelda");
                soc=strings(p+cursor,length,"fsl,imx7d");
                if(zelda!=1 || soc!=1) return KOA2_IDENTITY;
                compatible=1;
            }
            result.properties++; cursor+=padded;
        } else if(token==4) {
            /* NOP is permitted anywhere in the structure block. */
        } else if(token==9) {
            if(depth || !closed || cursor!=end) return KOA2_FORMAT;
            if(!compatible) return KOA2_IDENTITY;
            result.total=(uint32_t)bytes; *info=result; return KOA2_OK;
        } else return KOA2_FORMAT;
    }
    return KOA2_BOUNDS;
}
int koa2_range_valid(struct koa2_range v) { return v.size && v.start<=UINT64_MAX-v.size; }
int koa2_range_overlap(struct koa2_range a, struct koa2_range b) {
    if(!koa2_range_valid(a) || !koa2_range_valid(b)) return 1;
    return a.start<b.start+b.size && b.start<a.start+a.size;
}
int koa2_range_allowed(struct koa2_range p, const struct koa2_range *ram, size_t count,
                     const struct koa2_range *excluded, size_t n) {
    size_t i;
    int fits=0;
    if(!koa2_range_valid(p) || !ram || !count || (n&&!excluded)) return KOA2_RANGE;
    for(i=0;i<count;i++) {
        if(!koa2_range_valid(ram[i])) return KOA2_RANGE;
        if(p.start>=ram[i].start && p.start+p.size<=ram[i].start+ram[i].size) fits=1;
    }
    for(i=0;i<n;i++) if(koa2_range_overlap(p,excluded[i])) return KOA2_RANGE;
    return fits?KOA2_OK:KOA2_RANGE;
}
const struct koa2_range koa2_measured_ram[KOA2_MEASURED_RAM_COUNT]={
    {0x80000000u,0x1c000000u}, /* /proc/iomem System RAM 80000000-9bffffff */
    {0x9c500000u,0x03b00000u}, /* /proc/iomem System RAM 9c500000-9fffffff */
};
const struct koa2_range koa2_measured_excluded[KOA2_MEASURED_EXCLUDED_COUNT]={
    {0x80000000u,0x00c10000u}, /* running stock kernel: page tables at 0x80004000, code/data to 0x80c0facb */
    {0x9be00000u,0x00080000u}, /* Lab126 printk buffer and previous-boot copy, 2 x LOG_GOAL_SIZE */
    {0x9bf00000u,0x00100000u}, /* ramoops, boot log "attached 0x100000@0x9bf00000" */
    {0x9c000000u,0x00500000u}, /* DT reserved-memory/epdc_reserved, no-map */
    {0x9d681000u,0x0000d000u}, /* the FDT blob: memreserve #1 is U-Boot fdt_shrink_to_minimum's page-aligned
                                  0xc000, but FDT_RAMDISK_OVERHEAD makes totalsize 0xc080, so cover a page more */
    {0x9d691000u,0x0055aa00u}, /* FDT header memreserve #2: stock initrd, = /chosen initrd-start/end */
    {0x9ffc0000u,0x00040000u}, /* Falcon storage BIOS + BIOS (CONFIG_FALCON_*_ADDR/SIZE, memblock_reserve'd by
                                  fbios.c); stock eMMC access (falcon_blk, mmcblk2) executes code here */
};
int koa2_fdt_reservations(const void *input, size_t bytes,
                          struct koa2_range *out, size_t max, size_t *count) {
    const unsigned char *p=input;
    struct koa2_fdt_info info;
    size_t re,i;
    int result;
    if(count) *count=0;
    if(!count || (max && !out)) return KOA2_BOUNDS;
    result=koa2_fdt_validate(input,bytes,&info);
    if(result) return result;
    if(info.reservations>max) return KOA2_BOUNDS;
    re=be32(p+16);
    for(i=0;i<info.reservations;i++,re+=16) { out[i].start=be64(p+re); out[i].size=be64(p+re+8); }
    *count=info.reservations;
    return KOA2_OK;
}
int koa2_reservations_covered(const struct koa2_range *r, size_t n,
                              const struct koa2_range *x, size_t m) {
    size_t i,j;
    if((n && !r) || (m && !x)) return KOA2_RANGE;
    for(j=0;j<m;j++) if(!koa2_range_valid(x[j])) return KOA2_RANGE;
    for(i=0;i<n;i++) {
        int inside=0;
        if(!koa2_range_valid(r[i])) return KOA2_RANGE;
        for(j=0;j<m;j++)
            if(r[i].start>=x[j].start && r[i].start+r[i].size<=x[j].start+x[j].size) inside=1;
        if(!inside) return KOA2_RANGE;
    }
    return KOA2_OK;
}
