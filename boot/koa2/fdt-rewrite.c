/* SPDX-License-Identifier: GPL-2.0-only */
#include "fdt-rewrite.h"
#define RESERVE_MAX 64
enum { BEGIN=1, END_NODE=2, PROP=3, NOP=4, END=9 };
enum { NAME_BOOTARGS, NAME_START, NAME_END, NAMES };
static const char *const names[NAMES]={"bootargs","linux,initrd-start","linux,initrd-end"};
struct out { unsigned char *p; size_t max, at; int full; };
static uint32_t be32(const unsigned char *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void put32(unsigned char *p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static void emit(struct out *o, const void *data, size_t n) {
    if(o->full || n>o->max-o->at) { o->full=1; return; }
    memcpy(o->p+o->at,data,n); o->at+=n;
}
static void emit32(struct out *o, uint32_t v) { unsigned char b[4]; put32(b,v); emit(o,b,4); }
static void pad4(struct out *o) { static const unsigned char z[3]; emit(o,z,(4-(o->at&3))&3); }
static void prop(struct out *o, uint32_t name, const void *data, size_t n) {
    emit32(o,PROP); emit32(o,(uint32_t)n); emit32(o,name); emit(o,data,n); pad4(o);
}
/* Offset of an existing NUL-terminated name in the strings block (dtc shares
 * suffixes, so any position counts), or -1. */
static long find(const unsigned char *s, size_t n, const char *name) {
    size_t len=strlen(name)+1,i;
    for(i=0;i+len<=n;i++) if(!memcmp(s+i,name,len)) return (long)i;
    return -1;
}
static void chosen_props(struct out *o, const struct koa2_fdt_edit *e, const uint32_t *off) {
    prop(o,off[NAME_BOOTARGS],e->bootargs,strlen(e->bootargs)+1);
    if(e->initrd_end) {
        unsigned char v[4];
        put32(v,(uint32_t)e->initrd_start); prop(o,off[NAME_START],v,4);
        put32(v,(uint32_t)e->initrd_end); prop(o,off[NAME_END],v,4);
    }
}
static int edit_valid(const struct koa2_fdt_edit *e) {
    size_t i;
    if(!e || !e->bootargs || strlen(e->bootargs)>KOA2_BOOTARGS_MAX) return 0;
    /* The root of the KOA2 FDT has #address-cells = 1. */
    if(e->initrd_start || e->initrd_end)
        if(e->initrd_start>=e->initrd_end || e->initrd_end>UINT32_MAX) return 0;
    if(e->reserve_count>RESERVE_MAX || (e->reserve_count && !e->reserve)) return 0;
    for(i=0;i<e->reserve_count;i++) if(!koa2_range_valid(e->reserve[i])) return 0;
    return 1;
}
static int node_is(const char *name, const char *base) {
    size_t n=strlen(base);
    return !strncmp(name,base,n) && (name[n]==0 || name[n]=='@');
}
int koa2_fdt_rewrite(const void *input, size_t in_bytes, const struct koa2_fdt_edit *e,
                     void *output, size_t out_max, size_t *bytes, struct koa2_fdt_seen *seen) {
    const unsigned char *p=input;
    struct koa2_fdt_info info;
    struct koa2_fdt_seen saw={0,0,0,0};
    struct out o={output,out_max,0,0};
    size_t so,ss,no,ns,cursor,end,i,struct_at,strings_at,extra=0;
    uint32_t off[NAMES];
    unsigned depth=0;
    int in_chosen=0,in_memory=0,result;
    if(bytes) *bytes=0;
    if(seen) memset(seen,0,sizeof(*seen));
    if(!bytes || !seen || !output) return KOA2_BOUNDS;
    if((result=koa2_fdt_validate(input,in_bytes,&info))) return result;
    if(!edit_valid(e)) return KOA2_RANGE;
    so=be32(p+8); no=be32(p+12); ss=be32(p+36); ns=be32(p+32);
    for(i=0;i<NAMES;i++) {
        long at=find(p+no,ns,names[i]);
        if(at>=0) off[i]=(uint32_t)at;
        else { off[i]=(uint32_t)(ns+extra); extra+=strlen(names[i])+1; }
    }
    /* Header, then the new reserve map, 8-aligned at 40. */
    { static const unsigned char zero[40]; emit(&o,zero,40); }
    for(i=0;i<e->reserve_count;i++) {
        emit32(&o,(uint32_t)(e->reserve[i].start>>32)); emit32(&o,(uint32_t)e->reserve[i].start);
        emit32(&o,(uint32_t)(e->reserve[i].size>>32)); emit32(&o,(uint32_t)e->reserve[i].size);
    }
    { static const unsigned char zero[16]; emit(&o,zero,16); }
    struct_at=o.at;
    cursor=so; end=so+ss;
    while(cursor+4<=end && !o.full) {
        uint32_t token=be32(p+cursor); cursor+=4;
        if(token==BEGIN) {
            const char *name=(const char *)(p+cursor);
            size_t len=strlen(name),padded=(len+1+3)&~(size_t)3;
            emit32(&o,BEGIN); emit(&o,name,len+1); pad4(&o);
            cursor+=padded; depth++;
            in_chosen=depth==2 && node_is(name,"chosen");
            in_memory=depth==2 && node_is(name,"memory");
            if(in_chosen) { saw.chosen_nodes++; chosen_props(&o,e,off); }
        } else if(token==END_NODE) {
            if(depth==1 && !saw.chosen_nodes) {
                /* No /chosen in the input: add one as the root's last child. */
                emit32(&o,BEGIN); emit(&o,"chosen",7); pad4(&o);
                chosen_props(&o,e,off); emit32(&o,END_NODE);
            }
            emit32(&o,END_NODE); depth--; in_chosen=in_memory=0;
        } else if(token==PROP) {
            uint32_t length=be32(p+cursor),name=be32(p+cursor+4);
            const char *key=(const char *)(p+no+name);
            const unsigned char *data=p+cursor+8;
            size_t padded=((size_t)length+3)&~(size_t)3;
            int drop=0;
            cursor+=8+padded;
            if(in_chosen) for(i=0;i<NAMES;i++) if(!strcmp(key,names[i])) drop=1;
            if(in_memory && !strcmp(key,"reg")) {
                saw.memory_regs++;
                if(length==8) { saw.memory_start=be32(data); saw.memory_size=be32(data+4); }
            }
            if(!drop) prop(&o,name,data,length);
        } else if(token==NOP) {
            /* dropped */
        } else if(token==END) {
            emit32(&o,END); break;
        } else { o.full=1; break; }   /* unreachable: koa2_fdt_validate accepted it */
    }
    strings_at=o.at;
    emit(&o,p+no,ns);
    for(i=0;i<NAMES;i++) if(off[i]>=ns) emit(&o,names[i],strlen(names[i])+1);
    if(o.full) { memset(output,0,o.at); return KOA2_BOUNDS; }
    put32(o.p,0xd00dfeed); put32(o.p+4,(uint32_t)o.at);
    put32(o.p+8,(uint32_t)struct_at); put32(o.p+12,(uint32_t)strings_at);
    put32(o.p+16,40); put32(o.p+20,17); put32(o.p+24,16); put32(o.p+28,be32(p+28));
    put32(o.p+32,(uint32_t)(o.at-strings_at)); put32(o.p+36,(uint32_t)(strings_at-struct_at));
    if((result=koa2_fdt_validate(output,o.at,&info))) { memset(output,0,o.at); return result; }
    *bytes=o.at; *seen=saw;
    return KOA2_OK;
}
