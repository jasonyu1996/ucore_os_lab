# 物理页映射

为了在产生缺页异常时，能够为产生该异常用于内存访问的线性地址映射上物理页帧，我在`do_pgfault`函数中加入了如下操作：

* 调用`get_pte`函数获取该线性地址对应的二级页表项
* 判断该二级页表项的内容，对两类情况分别处理
    * 若二级页表项内容为0，该线性地址尚未映射到已经分配的物理页帧。这时调用`pgdir_alloc_page`函数将线性地址映射到新分配的物理页帧上（中间可能有页面的换出，`pgdir_alloc_page`会将该页帧设为可交换的（即将该页帧送给页面置换算法进行处理））
    * 若二级页表的内容不为0，该页表项对应的页帧已经被换出到外存中。这时调用`swap_in`函数将该页帧从外存中取出，存到一个物理页帧中（中间可能会有页面的换出），然后再用`page_insert`函数建立线性地址/虚拟页到该页帧的映射，用`swap_map_swappable`函数将该页帧送给页面置换算法进行处理以允许其在未来某个时候被换出。

页目录项和页表项中字段在页面置换算法中的作用？

页表项中如下字段在页面置换算法中起到了重要的作用：

* 存在位：硬件在页访问机制中会检查存在位，当遇一个存在位为0的页表项时，硬件会产生缺页异常，跳转到操作系统的中断服务例程运行。操作系统通过设置存在位，标志页表项指向的页是否在内存中。
* 脏位：通常操作系统会通过脏位来判断换出时是否将页帧写回外存，有的置换算法（例如extended clock）在选择换出页面时将脏位考虑进去。ucore操作系统似乎是完全没有管脏页，在换出时总会将页写回外存，FIFO页面置换算法也没有利用脏位。
* 访问位：大部分置换算法会考虑访问位，因为访问位为1表明页面在最近被访问过，通常也意味着页面更可能在近期内访问。
* 页帧号：当存在位为1时，表明页表项对应的物理页帧号。当所有标志位为0时，用于和为系统保留的字段一起记录换出的页在外存中的位置（实验中具体记录的是虚拟页号+1），或着当页表项所有位都为0时表示页表项未映射到物理页帧。

如果在缺页服务例程中出现了页访问异常，硬件会保存现场部分寄存器，将导致异常的线性地址存到CR2，然后再次跳转到中断服务例程执行。

# FIFO页面置换算法

这个算法非常容易实现。`map_swappable`就是直接用`list_add_before`将参数中的`Page`丢到循环链表尾部，而`swap_out_victim`就是从循环链表头部取出一个`Page`作为换出的victim。

现有的`swap_manager`可以支持extended clock算法。具体设计请见扩展实验中的描述。

被换出的页的脏位和访问位需要同时为0。ucore可以通过使用`get_pte`和`page->pra_vaddr`来获得页面对应的页表项，然后就可以通过页表项中的相应字段进行判断。

换入操作在发生缺页并且缺页服务例程发现缺页的原因是页面被换出到外存时进行，而换出操作在申请分配物理页帧发现无法满足需求时进行。这个在ucore的框架中已经实现。我认为这个与具体的页面置换算法关系并不大，页面置换算法只负责维护页面的一些信息，在需要换出时选择换出的页。


# 扩展实验：Extended Clock算法

我在本实验中实现了extended clock页面置换算法。具体实现细节可以在`kern/mm/swap_clockx.c`中找到。

## 设计与实现

我的实现是完全在现有的`swap_manager`框架下完成的，因此实现extended clock算法就等于实现`init_mm`、`map_swappable`、`swap_out_victim`等函数。

### 数据结构

我的extended clock实现中维护了与FIFO算法实现类似的数据结构：

    list_entry_t pra_list_head, *cur_pos;

其中，`pra_list_head`为所有可交换的页面组成的循环链表，`cur_pos`维护了一个在`pra_list_head`上移动的指针。置换算法将在移动这个`cur_pos`指针的同时依次对页面进行处理和判断。

### `init_mm`

这个函数做的事情就是初始化链表以及将`cur_pos`置为链表首部。

    static int
    _clockx_init_mm(struct mm_struct *mm)
    {     
        list_init(&pra_list_head);
        cur_pos = mm->sm_priv = &pra_list_head;
        
        return 0;
    }


### `map_swappable`

这个函数会将参数给出的页面插入到`cur_pos`之前，这样可以使新加入的页面在下一轮遍历过程中是最后一个被考虑的（因为它是最近才建立的页面，我们不希望它被立即换出）。

    static int
    _clockx_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
    {
        list_entry_t *entry=&(page->pra_page_link);
    
        assert(entry != NULL && cur_pos != NULL);
        list_add_before(cur_pos, entry);

        return 0;
    }

### `swap_out_victim`

这个函数会不断正向移动指针（`cur_pos=list_next(cur_pos)`），直到发现一个脏位和访问位均为0的页面。对不满足条件的页面，会按照如下方式对其页表项内容进行改写：

* 如果脏位和访问位均为1，则将访问位置0
* 如果脏位和访问位中恰有一个为1，则将二者均置0。如果脏位为1，其实感觉比较合理的做法是还要在这时将页面写回外存，但是ucore这块框架的做法是换出的页都会被写回（见`swap.c`中的`swap_out`函数），因而我在视线中没有考虑写回外存的问题。

见代码：

    if(PTE_A & *pte){
        *pte &= ~PTE_A;
    } else if(PTE_D & *pte){
        *pte &= ~PTE_D;
    } else
        break; // victim found

由页面数据结构`Page`获取页表项的方式为使用`get_pte`和`page->pra_vaddr`：

    page = le2page(cur_pos, pra_page_link);
    uintptr_t la = page->pra_vaddr;
    pte_t* pte = get_pte(mm->pgdir, la, 0);

在选择好换出的victim后，还需要将其从循环链表中删除。

## 测试

我使用了FIFO测试原有的测试样例，但是通过手工模拟extended clock算法对期望的答案进行了更改。详细情况请见`swap_clockx.c`文件中的`_clockx_check_swap`函数。

# 与参考答案的比较

## 物理页映射

与参考实现有一点比较重要的差异：参考实现多考虑了很多失败的边界情况。例如下面这段实现：

    ptep = get_pte(mm->pgdir, addr, 1);
    if(*ptep == 0){ // not mapped, create a new page
        pgdir_alloc_page(mm->pgdir, addr, perm);
    }
    ...

对应的参考实现是。

    if ((ptep = get_pte(mm->pgdir, addr, 1)) == NULL) {
        cprintf("get_pte in do_pgfault failed\n");
        goto failed;
    }
    if (*ptep == 0) { // if the phy addr isn't exist, then alloc a page & map the phy addr with logical addr
        if (pgdir_alloc_page(mm->pgdir, addr, perm) == NULL) {
            cprintf("pgdir_alloc_page in do_pgfault failed\n");
            goto failed;
        }
    }
    ...

这点差异并未在实验的测例中覆盖到。这些差异只会在一些特别的异常情况下才能产生。但是，可以想见，对于实际使用的操作系统来说，这样的边界条件判断是不可或缺的，因此，这点差异相当于提醒了我以后在重要的程序中不能忽略边界条件的处理。另外，可以发现，在加入这些特殊判断后，原本短小精简的代码变得略显繁琐和臃肿，这让我觉得在开发一个真正实用的操作系统时应该会有一些特殊的处理边界异常情况的机制。

## FIFO页面置换算法

这部分实现与参考实现没有任何值得一提的差异。

# 知识点

这次实验中涉及的知识点包括：

* 缺页服务例程中的操作。原理课只讲了缺页服务例程大体什么功能、在什么时候触发以及大概的执行流程等，并未让我们认识具体的实现长什么样。我认为实验很好地弥补了这点缺陷。
* 页面置换算法的原理和实现。原理课中介绍了若干种不同的算法，而实验仅涉及FIFO和extended clock两个算法。我个人觉得，FIFO算法和extended clock算法实现的实验能够让我们在系统编程上得到更多的练习，帮助我们认识页面置换算法在整个操作系统中的位置以及对操作系统其他模块的接口。但是，如果要我们更加深入地探讨众多页面置换算法的优劣，我觉得用更高层的软件模拟方式可能是一个更好的选择，因为这样更方便我们进行调试以及设计更加充分的测试样例。

我认为比较重要但是实验中没有涉及到的知识点包括：

* 页面置换算法的比较。如上，我认为应用更高层的模拟方式进行实验会更加方便。
    * Belady现象。可以与页面置换算法的实现实验相结合，设置实验鼓励大家构造样例证明Belady现象的存在。
* 抖动和负载控制
