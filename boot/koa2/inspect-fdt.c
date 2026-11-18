/* SPDX-License-Identifier: GPL-2.0-only */
#include "contract.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
    FILE *f;
    long n;
    void *data;
    struct koa2_fdt_info info;
    int result;
    if(argc!=2) { fprintf(stderr,"Usage: %s <DTB> (read-only structural check)\n",argv[0]); return 2; }
    f=fopen(argv[1],"rb");
    if(!f) { perror("open DTB"); return 2; }
    if(fseek(f,0,SEEK_END) || (n=ftell(f))<40 || n>16*1024*1024 || fseek(f,0,SEEK_SET)) {
        fclose(f); fprintf(stderr,"Invalid DTB length\n"); return 2;
    }
    data=malloc((size_t)n);
    if(!data) { fclose(f); return 2; }
    if(fread(data,1,(size_t)n,f)!=(size_t)n) { free(data); fclose(f); return 2; }
    fclose(f);
    result=koa2_fdt_validate(data,(size_t)n,&info); free(data);
    printf("{\"structural_result\":%d,\"bytes\":%u,\"nodes\":%u,\"properties\":%u,\"reservations\":%u,\"safe_to_boot\":false}\n",
           result,info.total,info.nodes,info.properties,info.reservations);
    return result?1:0;
}
