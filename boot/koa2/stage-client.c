/* SPDX-License-Identifier: GPL-2.0-only */
/* Drives /dev/koa2-handoff-stage: stages zImage, the stock FDT and the
 * initramfs, seals, prints the status, and closes (which frees everything).
 * It never asks the module to jump; the module has no way to.
 * usage: koa2-stage-client [--device PATH] zImage /sys/firmware/fdt initramfs */
#include "stage-abi.h"
#include "sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
_Static_assert(sizeof(struct k2s_begin)==116, "k2s_begin");
_Static_assert(sizeof(struct k2s_put)==24, "k2s_put");
_Static_assert(sizeof(struct k2s_status)==68, "k2s_status");
static unsigned char *slurp(const char *path, size_t *bytes) {
    /* /sys/firmware/fdt reports no size, so read until EOF. */
    int fd=open(path,O_RDONLY); size_t cap=1<<16,n=0; unsigned char *p=malloc(cap);
    if(fd<0 || !p) return NULL;
    for(;;) {
        ssize_t got;
        if(n==cap) { unsigned char *q; if(cap>=(size_t)512<<20 || !(q=realloc(p,cap*2))) { free(p); close(fd); return NULL; } p=q; cap*=2; }
        got=read(fd,p+n,cap-n);
        if(got<0) { free(p); close(fd); return NULL; }
        if(!got) break;
        n+=(size_t)got;
    }
    close(fd); *bytes=n; return p;
}
int main(int argc, char **argv) {
    struct k2s_begin b; struct k2s_status st;
    unsigned char *data[K2S_SLOTS]; size_t bytes[K2S_SLOTS];
    const char *device="/dev/koa2-handoff-stage";
    unsigned i; int fd,rc;
    if(argc==6 && !strcmp(argv[1],"--device")) { device=argv[2]; argv+=2; argc-=2; }
    if(argc!=4) { fprintf(stderr,"usage: %s [--device PATH] zImage fdt initramfs\n",argv[0]); return 2; }
    memset(&b,0,sizeof(b)); b.version=K2S_VERSION;
    for(i=0;i<K2S_SLOTS;++i) {
        SHA256_CTX c;
        if(!(data[i]=slurp(argv[1+i],&bytes[i])) || bytes[i]>0xffffffffu) { fprintf(stderr,"cannot read %s\n",argv[1+i]); return 2; }
        b.images[i].bytes=(k2s_u32)bytes[i];
        sha256_init(&c); sha256_update(&c,data[i],bytes[i]); sha256_final(&c,b.images[i].sha256);
    }
    if((fd=open(device,O_RDWR))<0) { perror("open"); return 1; }
    if(ioctl(fd,K2S_BEGIN,&b)) { perror("begin"); return 1; }
    for(i=0;i<K2S_SLOTS;++i) {
        size_t at=0;
        while(at<bytes[i]) {
            struct k2s_put p={i,(k2s_u32)at,(k2s_u32)(bytes[i]-at>65536?65536:bytes[i]-at),0,(k2s_u64)(unsigned long)(data[i]+at)};
            if(ioctl(fd,K2S_PUT,&p)) { perror("put"); return 1; }
            at+=p.bytes;
        }
    }
    rc=ioctl(fd,K2S_SEAL,0);
    if(rc) perror("seal");
    memset(&st,0,sizeof(st));
    if(ioctl(fd,K2S_STATUS,&st)) { perror("status"); return 1; }
    printf("state=%u plan_valid=%u execute_available=%u fdt_bytes=%u payload_pages=%u\n"
           "collision_pages=%u list_pages=%u list_entries=%u seal_stage=%d seal_result=%d\n",
           st.state,st.plan_valid,st.execute_available,st.fdt_bytes,st.payload_pages,
           st.collision_pages,st.list_pages,st.list_entries,st.seal_stage,st.seal_result);
    close(fd);
    return rc||!st.plan_valid;
}
