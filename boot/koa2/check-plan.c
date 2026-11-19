/* SPDX-License-Identifier: GPL-2.0-only */
/* Proposes and validates a KOA2 handoff layout for real files. Read-only.
 * usage: check-plan ZRELADDR KERNEL_END ZIMAGE_RUNTIME zImage fdt [initramfs]
 * (the three numbers from ports/koa2/derive-kernel-footprint.py) */
#include "plan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *slurp(const char *path, size_t *bytes) {
    FILE *f=fopen(path,"rb"); long n; unsigned char *p=NULL;
    if(!f) return NULL;
    if(!fseek(f,0,SEEK_END) && (n=ftell(f))>0 && n<64L*1024*1024 && !fseek(f,0,SEEK_SET) &&
       (p=malloc((size_t)n)) && fread(p,1,(size_t)n,f)==(size_t)n) *bytes=(size_t)n;
    else { free(p); p=NULL; }
    fclose(f);
    return p;
}
static const char *kind(enum koa2_segment_kind k) {
    return k==KOA2_SEG_ZIMAGE?"zImage":k==KOA2_SEG_FDT?"fdt":"initramfs";
}
int main(int argc, char **argv) {
    struct koa2_plan p;
    size_t zsize=0,dsize=0,rsize=0,i;
    unsigned char *z,*d,*r=NULL;
    struct koa2_fdt_info info;
    int result;
    if(argc!=6 && argc!=7) { fprintf(stderr,"usage: %s ZRELADDR KERNEL_END ZIMAGE_RUNTIME zImage fdt [initramfs]\n",argv[0]); return 2; }
    memset(&p,0,sizeof(p));
    p.footprint.zreladdr=strtoull(argv[1],NULL,0);
    p.footprint.kernel_end=strtoull(argv[2],NULL,0);
    p.footprint.zimage_runtime=strtoull(argv[3],NULL,0);
    z=slurp(argv[4],&zsize); d=slurp(argv[5],&dsize);
    if(argc==7) r=slurp(argv[6],&rsize);
    if(!z || !d || (argc==7 && !r)) { fprintf(stderr,"cannot read inputs\n"); return 2; }
    p.footprint.zimage_size=zsize;
    if((result=koa2_fdt_validate(d,dsize,&info))) { printf("fdt rejected: %d\n",result); return 1; }
    if((result=koa2_fdt_reservations(d,dsize,p.fdt_reservations,KOA2_PLAN_MAX_RESERVATIONS,&p.fdt_reservation_count))) {
        printf("fdt reserve map rejected: %d\n",result); return 1;
    }
    p.ram=koa2_measured_ram; p.ram_count=KOA2_MEASURED_RAM_COUNT;
    p.persistent=koa2_handoff_persistent; p.persistent_count=KOA2_HANDOFF_PERSISTENT_COUNT;
    p.lowmem_end=KOA2_LOWMEM_END;
    if(koa2_plan_propose(&p,dsize,rsize)) { puts("propose failed"); return 1; }
    for(i=0;i<p.segment_count;i++)
        printf("%-9s 0x%08llx-0x%08llx (%llu bytes)\n",kind(p.segments[i].kind),
               (unsigned long long)p.segments[i].dest,(unsigned long long)(p.segments[i].dest+p.segments[i].size-1),
               (unsigned long long)p.segments[i].size);
    for(i=0;i<p.fdt_reservation_count;i++)
        printf("fdt memreserve 0x%08llx+0x%llx\n",(unsigned long long)p.fdt_reservations[i].start,
               (unsigned long long)p.fdt_reservations[i].size);
    result=koa2_plan_validate(&p);
    printf("plan %s (%d)\n",result?"REJECTED":"valid",result);
    free(z); free(d); free(r);
    return result?1:0;
}
