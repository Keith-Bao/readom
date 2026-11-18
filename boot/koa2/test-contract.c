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
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb"); long bytes; unsigned char *p;
        assert(f && !fseek(f,0,SEEK_END)); bytes=ftell(f); assert(bytes>0 && bytes<16*1024*1024);
        rewind(f); p=malloc((size_t)bytes); assert(p && fread(p,1,(size_t)bytes,f)==(size_t)bytes); fclose(f);
        assert(koa2_fdt_validate(p,(size_t)bytes,&info)==0); free(p);
    }
    puts("KOA2 contract: truncation, bounds, identity, exclusions and 20000 mutations passed");
    return 0;
}
