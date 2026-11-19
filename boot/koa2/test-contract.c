/* SPDX-License-Identifier: GPL-2.0-only */
#include "contract.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void put(unsigned char *p,uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static size_t fixture(unsigned char *p) {
    static const char ids[]="fsl,imx7d-zelda\0fsl,imx7d";
    size_t so=56,cur=so;
    memset(p,0,256);
    put(p,0xd00dfeed); put(p+8,(uint32_t)so); put(p+16,40); put(p+20,17); put(p+24,16);
    put(p+cur,1); cur+=8; /* root name, including padding */
    put(p+cur,3); put(p+cur+4,sizeof(ids)); put(p+cur+8,0); cur+=12;
    memcpy(p+cur,ids,sizeof(ids)); cur+=(sizeof(ids)+3)&~(size_t)3;
    put(p+cur,2); cur+=4; put(p+cur,9); cur+=4;
    put(p+12,(uint32_t)cur); put(p+36,(uint32_t)(cur-so));
    memcpy(p+cur,"compatible",11); cur+=11; put(p+32,11); put(p+4,(uint32_t)cur);
    return cur;
}
static void put64(unsigned char *p,uint64_t v) { put(p,(uint32_t)(v>>32)); put(p+4,(uint32_t)v); }
/* Same tree as fixture(), with k header reservations before the structure block. */
static size_t fixture_res(unsigned char *p,const struct koa2_range *r,size_t k) {
    static const char ids[]="fsl,imx7d-zelda\0fsl,imx7d";
    size_t so=40+16*(k+1),cur=so,i;
    memset(p,0,512);
    put(p,0xd00dfeed); put(p+8,(uint32_t)so); put(p+16,40); put(p+20,17); put(p+24,16);
    for(i=0;i<k;i++) { put64(p+40+16*i,r[i].start); put64(p+48+16*i,r[i].size); }
    put(p+cur,1); cur+=8;
    put(p+cur,3); put(p+cur+4,sizeof(ids)); put(p+cur+8,0); cur+=12;
    memcpy(p+cur,ids,sizeof(ids)); cur+=(sizeof(ids)+3)&~(size_t)3;
    put(p+cur,2); cur+=4; put(p+cur,9); cur+=4;
    put(p+12,(uint32_t)cur); put(p+36,(uint32_t)(cur-so));
    memcpy(p+cur,"compatible",11); cur+=11; put(p+32,11); put(p+4,(uint32_t)cur);
    return cur;
}
static void measured_layout(void) {
    const struct koa2_range *ram=koa2_measured_ram,*x=koa2_measured_excluded;
    struct koa2_range got[4];
    unsigned char dtb[512];
    size_t i,j,n,count;
    /* The two runtime memreserves, and one the stock bootloader never made. */
    const struct koa2_range seen[]={{0x9d681000u,0xc000u},{0x9d691000u,0x55aa00u}};
    const struct koa2_range unknown[]={{0x9d681000u,0xc000u},{0x84000000u,0x1000u}};
    for(i=0;i<KOA2_MEASURED_RAM_COUNT;i++) {
        assert(koa2_range_valid(ram[i]));
        for(j=i+1;j<KOA2_MEASURED_RAM_COUNT;j++) assert(!koa2_range_overlap(ram[i],ram[j]));
    }
    for(i=0;i<KOA2_MEASURED_EXCLUDED_COUNT;i++) {
        int in_ram=0;
        assert(koa2_range_valid(x[i]));
        for(j=0;j<KOA2_MEASURED_RAM_COUNT;j++) if(koa2_range_overlap(x[i],ram[j])) in_ram=1;
        /* epdc_reserved is the hole between the RAM ranges; every other one is inside RAM. */
        assert(in_ram == !(x[i].start==0x9c000000u));
        for(j=i+1;j<KOA2_MEASURED_EXCLUDED_COUNT;j++) assert(!koa2_range_overlap(x[i],x[j]));
        /* Any payload touching an exclusion is refused, at either edge. */
        assert(koa2_range_allowed((struct koa2_range){x[i].start,1},ram,2,x,KOA2_MEASURED_EXCLUDED_COUNT)!=0);
        assert(koa2_range_allowed((struct koa2_range){x[i].start+x[i].size-1,1},ram,2,x,KOA2_MEASURED_EXCLUDED_COUNT)!=0);
    }
    /* Just past the running kernel is allowed; the RAM hole and the top are not. */
    assert(koa2_range_allowed((struct koa2_range){0x80c10000u,0x1000u},ram,2,x,KOA2_MEASURED_EXCLUDED_COUNT)==0);
    assert(koa2_range_allowed((struct koa2_range){0x80c0f000u,0x2000u},ram,2,x,KOA2_MEASURED_EXCLUDED_COUNT)!=0);
    assert(koa2_range_allowed((struct koa2_range){0x9fffff00u,0x200u},ram,2,x,KOA2_MEASURED_EXCLUDED_COUNT)!=0);
    /* Falcon BIOS sits in usable RAM but must survive: just below it is allowed, its first byte is not. */
    assert(koa2_range_allowed((struct koa2_range){0x9ffbf000u,0x1000u},ram,2,x,KOA2_MEASURED_EXCLUDED_COUNT)==0);
    assert(koa2_range_allowed((struct koa2_range){0x9ffbf000u,0x1001u},ram,2,x,KOA2_MEASURED_EXCLUDED_COUNT)!=0);
    /* Reserve map round trip, exact limit, and refusal past the limit. */
    n=fixture_res(dtb,seen,2);
    assert(koa2_fdt_reservations(dtb,n,got,2,&count)==0 && count==2);
    assert(got[0].start==seen[0].start && got[0].size==seen[0].size);
    assert(got[1].start==seen[1].start && got[1].size==seen[1].size);
    assert(koa2_fdt_reservations(dtb,n,got,1,&count)!=0 && count==0);
    assert(koa2_fdt_reservations(dtb,n,NULL,0,&count)!=0 && count==0);
    assert(koa2_reservations_covered(got,2,x,KOA2_MEASURED_EXCLUDED_COUNT)==0);
    n=fixture_res(dtb,unknown,2);
    assert(koa2_fdt_reservations(dtb,n,got,4,&count)==0 && count==2);
    assert(koa2_reservations_covered(got,2,x,KOA2_MEASURED_EXCLUDED_COUNT)!=0);
    n=fixture_res(dtb,NULL,0);
    assert(koa2_fdt_reservations(dtb,n,got,4,&count)==0 && count==0);
    assert(koa2_reservations_covered(got,0,x,KOA2_MEASURED_EXCLUDED_COUNT)==0);
    assert(koa2_fdt_reservations(dtb,n-1,got,4,&count)!=0 && count==0);
}
int main(int argc,char **argv) {
    unsigned char valid[256],bad[256];
    struct koa2_fdt_info info;
    struct koa2_range ram[]={{0x80000000,0x1c000000},{0x9c500000,0x3b00000}};
    struct koa2_range reserved[]={{0x9be00000,0x80000}};
    size_t n=fixture(valid),i;
    uint32_t state=1;
    assert(koa2_fdt_validate(valid,n,&info)==0 && info.nodes==1 && info.properties==1);
    for(i=0;i<n;i++) assert(koa2_fdt_validate(valid,i,&info)!=0);
    memcpy(bad,valid,n); put(bad+8,UINT32_MAX); assert(koa2_fdt_validate(bad,n,&info)!=0);
    memcpy(bad,valid,n); put(bad+12,56); assert(koa2_fdt_validate(bad,n,&info)!=0);
    memcpy(bad,valid,n); put(bad+16,56); assert(koa2_fdt_validate(bad,n,&info)!=0);
    memcpy(bad,valid,n); put(bad+68,UINT32_MAX); assert(koa2_fdt_validate(bad,n,&info)!=0);
    memcpy(bad,valid,n); bad[80]='x'; assert(koa2_fdt_validate(bad,n,&info)!=0);
    memcpy(bad,valid,n); put(bad+56,2); assert(koa2_fdt_validate(bad,n,&info)!=0);
    assert(koa2_fdt_validate(NULL,n,&info)!=0);
    assert(koa2_fdt_validate(valid,n,NULL)!=0);
    assert(koa2_range_allowed((struct koa2_range){0x81000000,0x1000},ram,2,reserved,1)==0);
    assert(koa2_range_allowed((struct koa2_range){0x9bfff000,0x600000},ram,2,NULL,0)!=0);
    assert(koa2_range_allowed((struct koa2_range){0x9be00000,0x1000},ram,2,reserved,1)!=0);
    assert(!koa2_range_valid((struct koa2_range){UINT64_MAX-1,3}));
    assert(!koa2_range_valid((struct koa2_range){0,0}));
    assert(!koa2_range_overlap((struct koa2_range){10,10},(struct koa2_range){20,10}));
    assert(koa2_range_overlap((struct koa2_range){10,11},(struct koa2_range){20,10}));
    for(i=0;i<20000;i++) {
        unsigned j;
        memcpy(bad,valid,n);
        for(j=0;j<4;j++) { state=state*1664525u+1013904223u; bad[state%n]^=(unsigned char)(state>>24); }
        (void)koa2_fdt_validate(bad,n,&info);
    }
    measured_layout();
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb"); long bytes; unsigned char *p;
        assert(f && !fseek(f,0,SEEK_END)); bytes=ftell(f); assert(bytes>0 && bytes<16*1024*1024);
        rewind(f); p=malloc((size_t)bytes); assert(p && fread(p,1,(size_t)bytes,f)==(size_t)bytes); fclose(f);
        assert(koa2_fdt_validate(p,(size_t)bytes,&info)==0);
        { struct koa2_range r[16]; size_t c;
          assert(koa2_fdt_reservations(p,(size_t)bytes,r,16,&c)==0);
          assert(koa2_reservations_covered(r,c,koa2_measured_excluded,KOA2_MEASURED_EXCLUDED_COUNT)==0); }
        free(p);
    }
    puts("KOA2 contract: truncation, bounds, identity, exclusions, measured layout, reserve map and 20000 mutations passed");
    return 0;
}
