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
static int next_transaction(int fd,uint32_t expected,struct binder_transaction_data *tr) {
    unsigned attempt;
    for(attempt=0;attempt<8;attempt++) {
        unsigned char buffer[256];
        struct binder_write_read wr={0};
        size_t offset=0;
        wr.read_size=sizeof(buffer); wr.read_buffer=(binder_uintptr_t)(uintptr_t)buffer;
        if(ioctl(fd,BINDER_WRITE_READ,&wr)) return -1;
        while(offset+4<=wr.read_consumed) {
            uint32_t command;
            memcpy(&command,buffer+offset,4); offset+=4;
            if(command==BR_NOOP || command==BR_SPAWN_LOOPER || command==BR_TRANSACTION_COMPLETE) continue;
            if(command!=expected || sizeof(*tr)>wr.read_consumed-offset) return -1;
            memcpy(tr,buffer+offset,sizeof(*tr)); return 0;
        }
    }
    return -1;
}
static int buffer_contains(void *mapping,binder_uintptr_t pointer,size_t size) {
    uintptr_t base=(uintptr_t)mapping,address=(uintptr_t)pointer;
    return mapping!=MAP_FAILED && size<=4096 && address>=base && address-base<=4096-size;
}
static void synchronous_fd_test(int server) {
    pid_t pid=fork();
    check(pid>=0,"fork synchronous FD client");
    if(pid==0) {
        int fd=device("binder"),ok=0;
        uint32_t request=0x1234,received=0;
        struct binder_transaction_data tr={0},reply;
        struct binder_fd_object object;
        void *mapping;
        if(fd<0) _exit(1);
        mapping=mmap(NULL,4096,PROT_READ,MAP_PRIVATE,fd,0);
        tr.code=0x7000; tr.flags=TF_ACCEPT_FDS; tr.data_size=sizeof(request);
        tr.data.ptr.buffer=(binder_uintptr_t)(uintptr_t)&request;
        if(mapping!=MAP_FAILED && !send_command(fd,BC_TRANSACTION,&tr,sizeof(tr)) &&
           !next_transaction(fd,BR_REPLY,&reply) && reply.data_size==sizeof(object) &&
           buffer_contains(mapping,reply.data.ptr.buffer,sizeof(object))) {
            memcpy(&object,(const void *)(uintptr_t)reply.data.ptr.buffer,sizeof(object));
            if(object.hdr.type==BINDER_TYPE_FD) {
                ok=read((int)object.fd,&received,sizeof(received))==sizeof(received) && received==0xfedcba98u;
                close((int)object.fd);
            }
            if(send_command(fd,BC_FREE_BUFFER,&reply.data.ptr.buffer,sizeof(reply.data.ptr.buffer))) ok=0;
        }
        if(mapping!=MAP_FAILED) munmap(mapping,4096);
        close(fd); _exit(ok?0:1);
    }
    if(pid>0) {
        struct binder_transaction_data request,reply={0};
        struct binder_fd_object object={0};
        binder_size_t offset=0;
        int descriptors[2],status=0;
        uint32_t data=0xfedcba98u,value=0;
        int received=next_transaction(server,BR_TRANSACTION,&request)==0;
        check(received,"receive synchronous request");
        if(received && buffer_contains(mappings[0],request.data.ptr.buffer,sizeof(value))) {
            memcpy(&value,(const void *)(uintptr_t)request.data.ptr.buffer,sizeof(value));
            check(request.code==0x7000 && request.data_size==sizeof(value) && value==0x1234 && request.sender_pid==pid,"synchronous payload and sender PID");
        }
        if(received && pipe(descriptors)==0) {
            check(write(descriptors[1],&data,sizeof(data))==sizeof(data),"prepare FD payload");
            close(descriptors[1]);
            object.hdr.type=BINDER_TYPE_FD; object.fd=(uint32_t)descriptors[0];
            reply.data_size=sizeof(object); reply.offsets_size=sizeof(offset);
            reply.data.ptr.buffer=(binder_uintptr_t)(uintptr_t)&object;
            reply.data.ptr.offsets=(binder_uintptr_t)(uintptr_t)&offset;
            check(send_command(server,BC_REPLY,&reply,sizeof(reply))==0,"reply with translated FD");
            close(descriptors[0]);
            check(send_command(server,BC_FREE_BUFFER,&request.data.ptr.buffer,sizeof(request.data.ptr.buffer))==0,"free synchronous request");
        }
        check(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0,"client reads payload from transferred FD");
    }
}
static void scatter_gather_test(int server) {
    pid_t pid=fork();
    check(pid>=0,"fork scatter-gather client");
    if(pid==0) {
        int fd=device("hwbinder"),ok;
        uint32_t payload[2]={0xabcdef01u,0x10203040u};
        binder_size_t offset=0;
        struct binder_buffer_object object={0};
        struct binder_transaction_data_sg sg={0};
        if(fd<0) _exit(1);
        object.hdr.type=BINDER_TYPE_PTR;
        object.buffer=(binder_uintptr_t)(uintptr_t)payload; object.length=sizeof(payload);
        sg.transaction_data.code=0x8000; sg.transaction_data.flags=TF_ONE_WAY;
        sg.transaction_data.data_size=sizeof(object); sg.transaction_data.offsets_size=sizeof(offset);
        sg.transaction_data.data.ptr.buffer=(binder_uintptr_t)(uintptr_t)&object;
        sg.transaction_data.data.ptr.offsets=(binder_uintptr_t)(uintptr_t)&offset;
        sg.buffers_size=sizeof(payload);
        ok=send_command(fd,BC_TRANSACTION_SG,&sg,sizeof(sg))==0;
        close(fd); _exit(ok?0:1);
    }
    if(pid>0) {
        struct binder_transaction_data tr;
        struct binder_buffer_object object;
        uint32_t payload[2]={0,0};
        int status=0,ok=0;
        int sent=waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0;
        check(sent,"client writes BC_TRANSACTION_SG");
        if(sent && next_transaction(server,BR_TRANSACTION,&tr)==0) {
            if(tr.code==0x8000 && tr.data_size==sizeof(object) &&
               buffer_contains(mappings[1],tr.data.ptr.buffer,sizeof(object))) {
                memcpy(&object,(const void *)(uintptr_t)tr.data.ptr.buffer,sizeof(object));
                if(object.hdr.type==BINDER_TYPE_PTR && object.length==sizeof(payload) &&
                   buffer_contains(mappings[1],object.buffer,sizeof(payload))) {
                    memcpy(payload,(const void *)(uintptr_t)object.buffer,sizeof(payload));
                    ok=payload[0]==0xabcdef01u && payload[1]==0x10203040u;
                }
            }
            if(send_command(server,BC_FREE_BUFFER,&tr.data.ptr.buffer,sizeof(tr.data.ptr.buffer))) ok=0;
        }
        check(ok,"scatter-gather buffer pointer relocated with exact payload");
    }
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
    if(!failures) synchronous_fd_test(fds[0]);
    if(!failures) scatter_gather_test(fds[1]);
    for(i=0;i<3;i++) {
        if(mappings[i] && mappings[i]!=MAP_FAILED) munmap(mappings[i],4096);
        if(fds[i]>=0) close(fds[i]);
    }
    printf("KOA2_BINDER_SMOKE_%s failures=%d\n",failures?"FAIL":"PASS",failures);
    sync(); reboot(RB_POWER_OFF); for(;;) pause();
}
