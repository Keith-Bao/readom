/* SPDX-License-Identifier: GPL-2.0-only */
/* QEMU-only PID 1. This program is not deployed to Kindle. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>
#include "uapi/binder.h"
static int failures;
static void *mappings[3];
static void check(int ok,const char *what) { printf("%s %s errno=%d\n",ok?"PASS":"FAIL",what,errno); if(!ok) failures++; }
static int device(const char *name) {
    char path[128];
    unsigned major,minor;
    FILE *f;
    snprintf(path,sizeof(path),"/sys/class/misc/%s/dev",name);
    f=fopen(path,"r");
    if(!f || fscanf(f,"%u:%u",&major,&minor)!=2) { if(f) fclose(f); return -1; }
    fclose(f); snprintf(path,sizeof(path),"/dev/%s",name);
    if(mknod(path,S_IFCHR|0600,makedev(major,minor)) && errno!=EEXIST) return -1;
    return open(path,O_RDWR|O_CLOEXEC);
}
static int send_command(int fd,uint32_t command,const void *data,size_t size) {
    unsigned char buffer[256];
    struct binder_write_read wr={0};
    if(size>sizeof(buffer)-sizeof(command)) return -1;
    memcpy(buffer,&command,sizeof(command));
    if(size) memcpy(buffer+sizeof(command),data,size);
    wr.write_size=sizeof(command)+size; wr.write_buffer=(binder_uintptr_t)(uintptr_t)buffer;
    return ioctl(fd,BINDER_WRITE_READ,&wr)==0 && wr.write_consumed==wr.write_size ? 0:-1;
}
static int receive_value(int fd,int index) {
    unsigned char buffer[256];
    struct binder_write_read wr={0};
    size_t offset=0;
    wr.read_size=sizeof(buffer); wr.read_buffer=(binder_uintptr_t)(uintptr_t)buffer;
    if(ioctl(fd,BINDER_WRITE_READ,&wr)) return -1;
    while(offset+4<=wr.read_consumed) {
        uint32_t command;
        memcpy(&command,buffer+offset,4); offset+=4;
        if(command==BR_NOOP || command==BR_SPAWN_LOOPER) continue;
        if(command==BR_TRANSACTION && sizeof(struct binder_transaction_data)<=wr.read_consumed-offset) {
            struct binder_transaction_data tr;
            uint32_t value=0;
            uintptr_t base=(uintptr_t)mappings[index],addr;
            memcpy(&tr,buffer+offset,sizeof(tr)); addr=(uintptr_t)tr.data.ptr.buffer;
            if(tr.code!=(uint32_t)(0x6000+index) || tr.data_size!=sizeof(value) ||
               addr<base || addr-base>4096-sizeof(value)) return -1;
            memcpy(&value,(const void *)addr,sizeof(value));
            if(send_command(fd,BC_FREE_BUFFER,&tr.data.ptr.buffer,sizeof(tr.data.ptr.buffer))) return -1;
            return value==(uint32_t)(0xc0de0000u+index)?0:-1;
        }
        return -1;
    }
    return -1;
}
int main(void) {
    const char *names[]={"binder","hwbinder","vndbinder"};
    int fds[3],i;
    char cmdline[4096]={0};
    FILE *f;
    if(getpid()!=1) { fprintf(stderr,"QEMU PID 1 only\n"); return 2; }
    setbuf(stdout,NULL);
    mkdir("/proc",0755); mkdir("/sys",0755); mkdir("/dev",0755);
    mount("proc","/proc","proc",0,NULL); mount("sysfs","/sys","sysfs",0,NULL);
    f=fopen("/proc/cmdline","r");
    if(f) { if(!fgets(cmdline,sizeof(cmdline),f)) cmdline[0]=0; fclose(f); }
    if(!strstr(cmdline,"koa2_binder_smoke=1")) { puts("QEMU smoke flag absent"); reboot(RB_POWER_OFF); for(;;) pause(); }
    for(i=0;i<3;i++) {
        struct binder_version version={0};
        void *mapped;
        fds[i]=device(names[i]); check(fds[i]>=0,names[i]);
        if(fds[i]<0) continue;
        check(ioctl(fds[i],BINDER_VERSION,&version)==0 && version.protocol_version==7,"32-bit Binder protocol 7");
        mapped=mmap(NULL,4096,PROT_READ,MAP_PRIVATE,fds[i],0);
        check(mapped!=MAP_FAILED,"read-only Binder mmap");
        mappings[i]=mapped;
        errno=0;
        check(ioctl(fds[i],BINDER_SET_CONTEXT_MGR,0)==0,"independent context manager");
        check(send_command(fds[i],BC_ENTER_LOOPER,NULL,0)==0,"enter Binder server looper");
    }
    /* Each separate context is occupied: a new process must be rejected. */
    {
        pid_t pid=fork();
        check(pid>=0,"fork context contender");
        if(pid==0) {
            int bad=0;
            for(i=0;i<3;i++) {
                int fd=device(names[i]);
                if(fd<0) { bad=1; continue; }
                errno=0;
                if(ioctl(fd,BINDER_SET_CONTEXT_MGR,0)!=-1 || errno!=EBUSY) bad=1;
                {
                    uint32_t value=0xc0de0000u+i;
                    struct binder_transaction_data tr={0};
                    tr.target.handle=0; tr.code=0x6000+i; tr.flags=TF_ONE_WAY;
                    tr.data_size=sizeof(value); tr.data.ptr.buffer=(binder_uintptr_t)(uintptr_t)&value;
                    if(send_command(fd,BC_TRANSACTION,&tr,sizeof(tr))) bad=1;
                }
                close(fd);
            }
            _exit(bad?1:0);
        }
        if(pid>0) {
            int status=0;
            int ok=waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0;
            check(ok,"context ownership and client writes");
            if(ok) for(i=0;i<3;i++) check(receive_value(fds[i],i)==0,"one-way transaction payload stays in its context");
        }
    }
    for(i=0;i<3;i++) {
        if(mappings[i] && mappings[i]!=MAP_FAILED) munmap(mappings[i],4096);
        if(fds[i]>=0) close(fds[i]);
    }
    printf("KOA2_BINDER_SMOKE_%s failures=%d\n",failures?"FAIL":"PASS",failures);
    sync(); reboot(RB_POWER_OFF); for(;;) pause();
}
