/* SPDX-License-Identifier: GPL-2.0-only */
/* Proposes a KOA2 handoff layout for real files, rewrites the stock FDT for it
 * and validates the result. Reads its inputs, writes only OUT_FDT.
 * usage: check-plan ZRELADDR KERNEL_END ZIMAGE_RUNTIME zImage stock.dtb initramfs BOOTARGS OUT_FDT
 * (the three numbers from ports/koa2/derive-kernel-footprint.py; initramfs "-" for none) */
#include "plan.h"
#include "fdt-rewrite.h"
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
    struct koa2_fdt_edit e;
    struct koa2_fdt_seen seen;
    size_t zsize=0,dsize=0,rsize=0,osize=0,first=0,i;
    unsigned char *z,*d,*r=NULL,*o;
    int result;
    FILE *f;
    if(argc!=9) {
        fprintf(stderr,"usage: %s ZRELADDR KERNEL_END ZIMAGE_RUNTIME zImage stock.dtb initramfs|- BOOTARGS OUT_FDT\n",argv[0]);
        return 2;
    }
    memset(&p,0,sizeof(p));
    p.footprint.zreladdr=strtoull(argv[1],NULL,0);
    p.footprint.kernel_end=strtoull(argv[2],NULL,0);
    p.footprint.zimage_runtime=strtoull(argv[3],NULL,0);
    z=slurp(argv[4],&zsize); d=slurp(argv[5],&dsize);
    if(strcmp(argv[6],"-")) r=slurp(argv[6],&rsize);
    if(!z || !d || (strcmp(argv[6],"-") && !r) || !(o=malloc(dsize+8192))) { fprintf(stderr,"cannot read inputs\n"); return 2; }
    p.footprint.zimage_size=zsize;
    p.ram=koa2_measured_ram; p.ram_count=KOA2_MEASURED_RAM_COUNT;
    p.persistent=koa2_handoff_persistent; p.persistent_count=KOA2_HANDOFF_PERSISTENT_COUNT;
    p.lowmem_end=KOA2_LOWMEM_END;
    /* The new reserve map is empty: the new kernel reserves the FDT itself
     * (early_init_fdt_scan_reserved_mem), the initramfs from /chosen, the printk
     * buffer, ramoops and Falcon BIOS in code, and EPDC from reserved-memory. */
    memset(&e,0,sizeof(e)); e.bootargs=argv[7];
    if(r) { e.initrd_start=0x1000; e.initrd_end=0x2000; }   /* placeholders: same size as the real values */
    if((result=koa2_fdt_rewrite(d,dsize,&e,o,dsize+8192,&first,&seen))) { printf("fdt rewrite failed: %d\n",result); return 1; }
    if(koa2_plan_propose(&p,first,rsize)) { puts("propose failed"); return 1; }
    if(r) { e.initrd_start=p.segments[2].dest; e.initrd_end=p.segments[2].dest+rsize; }
    if((result=koa2_fdt_rewrite(d,dsize,&e,o,dsize+8192,&osize,&seen)) || osize!=first) {
        printf("fdt rewrite failed: %d\n",result); return 1;
    }
    /* /memory is copied as is: the EPDC no-map node makes the measured hole. */
    if(seen.memory_regs!=1 || seen.memory_start!=0x80000000u || seen.memory_size!=0x20000000u) {
        puts("unexpected /memory"); return 1;
    }
    if((result=koa2_fdt_reservations(o,osize,p.fdt_reservations,KOA2_PLAN_MAX_RESERVATIONS,&p.fdt_reservation_count))) {
        printf("fdt reserve map rejected: %d\n",result); return 1;
    }
    for(i=0;i<p.segment_count;i++)
        printf("%-9s 0x%08llx-0x%08llx (%llu bytes)\n",kind(p.segments[i].kind),
               (unsigned long long)p.segments[i].dest,(unsigned long long)(p.segments[i].dest+p.segments[i].size-1),
               (unsigned long long)p.segments[i].size);
    printf("fdt %zu -> %zu bytes, %zu memreserve, /chosen %s\n",dsize,osize,p.fdt_reservation_count,
           seen.chosen_nodes?"replaced":"added");
    result=koa2_plan_validate(&p);
    printf("plan %s (%d)\n",result?"REJECTED":"valid",result);
    if(!result && (!(f=fopen(argv[8],"wb")) || fwrite(o,1,osize,f)!=osize || fclose(f))) { puts("cannot write output"); return 2; }
    free(z); free(d); free(r); free(o);
    return result?1:0;
}
