/* SPDX-License-Identifier: GPL-2.0-only */
/* Just enough of the Linux 4.1 module API for boot/koa2/stage.c to run on the
 * host under ASan. Pages come from a script the test controls, so it can say
 * which physical addresses the allocator hands out. */
#ifndef KSHIM_H
#define KSHIM_H
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define CONFIG_ARM 1
#define PAGE_SHIFT 12
#define PAGE_SIZE 4096u
#define __user
#define __init
#define __exit
#define GFP_KERNEL 1u
#define __GFP_ZERO 2u
#define __GFP_NORETRY 4u
#define __GFP_NOWARN 8u
#define __GFP_HIGHMEM 16u
#define PAGE_KERNEL 0
#define CAP_SYS_BOOT 22
#define FMODE_WRITE 2u
#define MISC_DYNAMIC_MINOR 255
#define THIS_MODULE NULL
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define BUILD_BUG_ON(c) do { _Static_assert(!(c),#c); } while(0)
#define module_param(n,t,p) extern int kshim_unused_##n
#define MODULE_LICENSE(x) extern int kshim_unused_license
#define MODULE_DESCRIPTION(x) extern int kshim_unused_description
#define module_init(f) int (*kshim_init)(void)=f
#define module_exit(f) void (*kshim_exit)(void)=f
extern unsigned long kshim_phys_offset;
#define PHYS_OFFSET kshim_phys_offset
#define PAGE_OFFSET 0x80000000u
struct page { u64 phys; unsigned char *data; struct page *next; };
struct page *alloc_page(unsigned int gfp);
void __free_page(struct page *p);
static inline void *page_address(struct page *p) { return p->data; }
u64 kshim_virt_to_phys(const void *v);
#define virt_to_phys(v) kshim_virt_to_phys(v)
static inline void *vzalloc(unsigned long size) { return calloc(1,size); }
static inline void vfree(void *p) { free(p); }
static inline void *kmalloc(size_t n, unsigned int gfp) { (void)gfp; return malloc(n); }
static inline void *kzalloc(size_t n, unsigned int gfp) { (void)gfp; return calloc(1,n); }
static inline void kfree(void *p) { free(p); }
extern const void *kshim_fault_source;
static inline unsigned long copy_from_user(void *d, const void *s, unsigned long n)
{ if(s==kshim_fault_source) return n; memcpy(d,s,n); return 0; }
static inline unsigned long copy_to_user(void *d, const void *s, unsigned long n) { memcpy(d,s,n); return 0; }
extern int kshim_capable;
static inline int capable(int c) { (void)c; return kshim_capable; }
struct mutex { int held; };
static inline void mutex_init(struct mutex *m) { m->held=0; }
static inline int mutex_lock_interruptible(struct mutex *m) { m->held=1; return 0; }
static inline void mutex_unlock(struct mutex *m) { m->held=0; }
typedef struct { int counter; } atomic_t;
#define ATOMIC_INIT(v) {v}
static inline int atomic_cmpxchg(atomic_t *a, int old, int new_value)
{ int was=a->counter; if(was==old) a->counter=new_value; return was; }
static inline void atomic_set(atomic_t *a, int v) { a->counter=v; }
struct new_utsname { char release[65], version[65], machine[65]; };
extern struct new_utsname kshim_uts;
static inline const struct new_utsname *init_utsname(void) { return &kshim_uts; }
struct inode { int unused; };
struct file { unsigned int f_mode; void *private_data; };
struct file_operations {
    void *owner;
    int (*open)(struct inode *, struct file *);
    int (*release)(struct inode *, struct file *);
    long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
    long long (*llseek)(struct file *, long long, int);
};
static inline long long no_llseek(struct file *f, long long o, int w) { (void)f; (void)o; (void)w; return -ESPIPE; }
struct miscdevice { int minor; const char *name; const struct file_operations *fops; int mode; };
extern struct miscdevice *kshim_misc;
static inline int misc_register(struct miscdevice *m) { kshim_misc=m; return 0; }
static inline void misc_deregister(struct miscdevice *m) { (void)m; kshim_misc=NULL; }
#endif
