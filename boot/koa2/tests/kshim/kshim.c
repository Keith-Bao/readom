/* SPDX-License-Identifier: GPL-2.0-only */
#include "kshim.h"
unsigned long kshim_phys_offset=0x80000000u;
const void *kshim_fault_source;
int kshim_capable=1;
struct new_utsname kshim_uts;
struct miscdevice *kshim_misc;
/* The allocator hands out consecutive physical pages from kshim_next, and
 * fails once kshim_budget reaches 0 (negative: never). */
u64 kshim_next=0x90000000u;
long kshim_budget=-1;
long kshim_live;
/* Live pages, so virt_to_phys can map a page's data back to its address. */
static struct page *live;
struct page *alloc_page(unsigned int gfp)
{
    struct page *p;
    (void)gfp;
    if(!kshim_budget) return NULL;
    if(kshim_budget>0) kshim_budget--;
    p=calloc(1,sizeof(*p));
    if(!p || !(p->data=calloc(1,PAGE_SIZE))) { free(p); return NULL; }
    p->phys=kshim_next; kshim_next+=PAGE_SIZE; kshim_live++;
    p->next=live; live=p;
    return p;
}
void __free_page(struct page *p)
{
    struct page **at=&live;
    while(*at!=p) at=&(*at)->next;
    *at=p->next;
    free(p->data); free(p); kshim_live--;
}
/* The clock, a stand-in for the Lab126 printk buffer, and the cache
 * maintenance. */
unsigned long kshim_jiffies;
unsigned char *kshim_printk_buffer;
u64 kshim_printk_phys;
size_t kshim_printk_bytes;
unsigned kshim_write_backs;
u64 kshim_cleaned_phys;
/* Only the buffer the test set up. Any other physical address is one the module
 * had no business turning into a pointer, so fail loudly instead of handing
 * back something that happens to work. */
void *kshim_phys_to_virt(u64 phys)
{
    if(kshim_printk_buffer && phys>=kshim_printk_phys && phys<kshim_printk_phys+kshim_printk_bytes)
        return kshim_printk_buffer+(size_t)(phys-kshim_printk_phys);
    abort();
}
void flush_cache_all(void) { kshim_write_backs++; }
void outer_clean_range(u64 start, u64 end) { kshim_cleaned_phys=start; (void)end; }
int printk(const char *format, ...) { (void)format; return 0; }
u64 kshim_virt_to_phys(const void *v)
{
    struct page *p;
    if(v==(const void *)(uintptr_t)PAGE_OFFSET) return kshim_phys_offset;
    for(p=live;p;p=p->next) if((const unsigned char *)v>=p->data && (const unsigned char *)v<p->data+PAGE_SIZE)
        return p->phys+(u64)((const unsigned char *)v-p->data);
    abort();
}
/* Exec dry-run stand-ins (boot/koa2/exec.c). */
int kshim_cpu_online[2]={1,1};
unsigned kshim_irq_masked, kshim_fiq_masked, kshim_irq_depth;
unsigned kshim_cpsr, kshim_sctlr, kshim_ttbcr;
void flush_icache_range(unsigned long start, unsigned long end) { (void)start; (void)end; }
