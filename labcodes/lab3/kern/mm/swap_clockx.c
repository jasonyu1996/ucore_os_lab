#include<swap_clockx.h>
#include <defs.h>
#include <x86.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <list.h>
#include <swapfs.h>

list_entry_t pra_list_head, *cur_pos;

static int
_clockx_init_mm(struct mm_struct *mm)
{     
     list_init(&pra_list_head);
     cur_pos = mm->sm_priv = &pra_list_head;
     
     return 0;
}


static int
_clockx_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    list_entry_t *entry=&(page->pra_page_link);
 
    assert(entry != NULL && cur_pos != NULL);
    list_add_before(cur_pos, entry);

    return 0;
}

static int
_clockx_swap_out_victim(struct mm_struct *mm, struct Page ** ptr_page, int in_tick)
{
     list_entry_t *head=(list_entry_t*) mm->sm_priv;
         assert(head != NULL);
     assert(in_tick==0);
     /* Select the victim */
     /*LAB3 EXERCISE 2: YOUR CODE*/ 
     //(1)  unlink the  earliest arrival page in front of pra_list_head qeueue
     //(2)  set the addr of addr of this page to ptr_page

    struct Page* page;

    while(1){
        if(cur_pos != &pra_list_head){
            page = le2page(cur_pos, pra_page_link);
            uintptr_t la = page->pra_vaddr;
            pte_t* pte = get_pte(mm->pgdir, la, 0);

            if(PTE_A & *pte){
                *pte &= ~PTE_A;
            } else if(PTE_D & *pte){
                // dirty
                // not necessary to write out
                *pte &= ~PTE_D;
            } else
                break; // victim found

        // find the page table entry
        }
        cur_pos = list_next(cur_pos);
    }

    
    *ptr_page = page;

    list_entry_t* nxt_pos = list_next(cur_pos);

    list_del(cur_pos);

    cur_pos = nxt_pos;

     return 0;
}

static int
_clockx_check_swap(void) {
    cprintf("write Virt Page c in clockx_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num<=4);
    cprintf("write Virt Page a in clockx_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num<=4);
    cprintf("write Virt Page d in clockx_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num<=4);
    cprintf("write Virt Page b in clockx_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num<=4);
    cprintf("write Virt Page e in clockx_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num<=5);
    cprintf("write Virt Page b in clockx_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num<=5);
    cprintf("write Virt Page a in clockx_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num<=6);
    cprintf("write Virt Page b in clockx_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num<=7);
    cprintf("write Virt Page c in clockx_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num<=8);
    cprintf("write Virt Page d in clockx_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num<=9);
    cprintf("write Virt Page e in clockx_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num<=10);
    cprintf("write Virt Page a in clockx_check_swap\n");
    assert(*(unsigned char *)0x1000 == 0x0a);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num<=11);
    return 0;
}


static int
_clockx_init(void)
{
    return 0;
}

static int
_clockx_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    return 0;
}

static int
_clockx_tick_event(struct mm_struct *mm)
{ return 0; }




struct swap_manager swap_manager_clockx =
{
     .name            = "extended clock swap manager",
     .init            = &_clockx_init,
     .init_mm         = &_clockx_init_mm,
     .tick_event      = &_clockx_tick_event,
     .map_swappable   = &_clockx_map_swappable,
     .set_unswappable = &_clockx_set_unswappable,
     .swap_out_victim = &_clockx_swap_out_victim,
     .check_swap      = &_clockx_check_swap,
};
