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
u64 kshim_virt_to_phys(const void *v)
{
    struct page *p;
    if(v==(const void *)(uintptr_t)PAGE_OFFSET) return kshim_phys_offset;
    for(p=live;p;p=p->next) if((const unsigned char *)v>=p->data && (const unsigned char *)v<p->data+PAGE_SIZE)
        return p->phys+(u64)((const unsigned char *)v-p->data);
    abort();
}
