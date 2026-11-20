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
struct page *alloc_page(unsigned int gfp)
{
    struct page *p;
    (void)gfp;
    if(!kshim_budget) return NULL;
    if(kshim_budget>0) kshim_budget--;
    p=calloc(1,sizeof(*p));
    if(!p || !(p->data=calloc(1,PAGE_SIZE))) { free(p); return NULL; }
    p->phys=kshim_next; kshim_next+=PAGE_SIZE; kshim_live++;
    return p;
}
void __free_page(struct page *p) { free(p->data); free(p); kshim_live--; }
void *__vmalloc(unsigned long size, unsigned int gfp, int prot) { (void)gfp; (void)prot; return calloc(1,size); }
