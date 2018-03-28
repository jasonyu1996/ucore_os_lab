# 分配并初始化进程控制块

我实现的进程初始化过程中主要先后做了下面几个操作：

* 清空进程控制块内的所有内容
* 将进程的状态设置为`PROC_UNINIT`，表示进程还未完成初始化
* 将进程的父进程设置为当前进程（控制块的`parent`字段指向`current`进程控制块）
* 将进程的CR3寄存器内容设置为内核使用的CR3内容，表示进程使用内核的页表

进程控制块数据结构`proc_struct`中包含的`context`和`tf`字段的作用如下：

* `context`字段保存的进程所处上下文中寄存器的值。在发生上下文切换时，被换出的`context`会保存当前各个寄存器的值，被换入的`context`会被加载。下面的`switch.S`中的代码就是在做这件事：
        
        switch_to:                      # switch_to(from, to)

        # save from's registers
        movl 4(%esp), %eax          # eax points to from
        popl 0(%eax)                # save eip !popl
        movl %esp, 4(%eax)          # save esp::context of from
        movl %ebx, 8(%eax)          # save ebx::context of from
        movl %ecx, 12(%eax)         # save ecx::context of from
        movl %edx, 16(%eax)         # save edx::context of from
        movl %esi, 20(%eax)         # save esi::context of from
        movl %edi, 24(%eax)         # save edi::context of from
        movl %ebp, 28(%eax)         # save ebp::context of from

        # restore to's registers
        movl 4(%esp), %eax          # not 8(%esp): popped return address already
                                    # eax now points to to
        movl 28(%eax), %ebp         # restore ebp::context of to
        movl 24(%eax), %edi         # restore edi::context of to
        movl 20(%eax), %esi         # restore esi::context of to
        movl 16(%eax), %edx         # restore edx::context of to
        movl 12(%eax), %ecx         # restore ecx::context of to
        movl 8(%eax), %ebx          # restore ebx::context of to
        movl 4(%eax), %esp          # restore esp::context of to

* `tf`字段保存了进入进程时使用的trapframe，其中就包含了特权级的信息等。因为操作系统要实现特权级的转换，需要借助中断返回中从trapframe恢复寄存器的机制。`tf`在`trapentry.S`中被使用到：

    forkrets:
    movl 4(%esp), %esp
    jmp __trapret

其中，`movl 4(%esp), %esp`将`tf`地址赋给栈顶指针，这样可以使`tf`成为栈的顶部，在跳转到`__trapret`后可以利用`iret`实现特权级的转换：

    __trapret:
        # restore registers from stack
        popal

        # restore %ds, %es, %fs and %gs
        popl %gs
        popl %fs
        popl %es
        popl %ds

        # get rid of the trap number and error code
        addl $0x8, %esp
        iret



# 为新创建的内核线程分配资源

在我的实现中，`do_fork`函数对新创建的内核线程做了如下操作：

* 分配pid
* 加入进程的散列表和链表
* 为进程分配内核栈空间（使用`setup_kstack`函数）
* 调用`copy_mm`复制与内存管理相关的信息
* 调用`copy_thread`设置好进入该进程时使用的trapframe以及该进程上下文的栈顶和eip
* 将进程状态置为`RUNNABLE`



请说明ucore是否做到给每个新fork的线程一个唯一的id？请说明你的分析和理由。

通过观察`proc.c`中的`get_pid`函数，我发现ucore能够做到给每个新fork的线程一个唯一的id，只要进程对`get_pid`函数的调用和对`proc_list`的写入是互斥进行的，而在lab4中这部分满足互斥的条件，因为现在没有抢占机制，一个进程执行完成后才会切换到另外一个进程。分析`get_pid`的下面这段代码中可以发现，当且仅当`last_pid`不与`list`中的任何一个进程的`pid`相等时才会退出循环。

    repeat:
        le = list;
        while ((le = list_next(le)) != list) {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid) {
                if (++ last_pid >= next_safe) {
                    if (last_pid >= MAX_PID) {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid) {
                next_safe = proc->pid;
            }
        }

这段代码从小到大枚举`last_pid`，然后进入内层循环检查其是否与`list`中的某个进程的`pid`相等。如果找到了这样的进程，则需要令`last_pid=last_pid+1`继续枚举的过程。这里代码用`next_safe`变量维护已经检查的进程`pid`的最小值，利用它对这个过程做了一点优化：当发现相等`pid`的情况时，如果之前已经检查的进程`pid`的最小值大于下一个枚举的`last_pid`，则可以肯定已经检查的进程中一定不存在`pid==last_pid`，因此可以继续进行检查而无需返回`list`头部重新开始检查。

# 进程切换

通过阅读进程切换相关的代码，可以发现进入一个进程的过程大致包括了如下步骤：

    schedule -> proc_run（换页表） -> switch_to（恢复context） -> forkret -> forkrets（特权级转换） -> kernel_thread_entry -> main函数 -> main函数返回到kernel_thread_entry -> do_exit（这里直接panic了）

其中，`proc_run`函数主要做了如下几件事情：

* 设置TSS中的0特权级的栈顶指针，以使后续在该进程执行过程中产生中断发生用户态转到核心态的转换时能够使用单独的栈空间
* 将即将运行的进程的页目录表基地址装入CR3寄存器
* 调用`switch_to`进行上下文切换（后续执行流程见上面）

本实验创建并运行了`idleproc`和`initproc`两个内核线程。其中`idleproc`其实就对应进入内核时的那条指令流，因为`proc_init`函数中有

    current = idleproc;

在完成了各种初始化工作后，`idleproc`会进入`cpu_idle`函数。这个函数干的事情就是轮询进程列表，找到一个`RUNNABLE`的，然后把就把资源丢出去（切换到那个进程）。

`initproc`在这个实验中其实除了输出三行字外什么都没有干。它就是在`idleproc`进入`cpu_idle`时被切换进来执行的。整个内核的正常执行在`initproc`执行结束后就结束了，因为现在的`do_exit`干的事情是直接panic。这样，`idleproc`其实没有机会再次拿到执行权。

`local_intr_save`和`local_intr_restore`干的事情是将设置中断使能位，其中`local_intr_save`将中断使能位置零（禁止中断），并将中断使能位原来的值保存到指定的变量中，`local_intr_restore`则是根据指定的变量恢复中断使能位。也就是说，如果本来中断是使能的，在`local_intr_save`和`local_intr_restore`之间中断会被禁止。这是一种互斥执行的实现方式，因为中断禁止状态下可以保证执行过程不会被打断，不会发生上下文切换，不会有任何共享资源的状态被别的进程改变。因此，这两个语句的作用相当于将中间执行的操作变成原子操作。

从`local_intr_save`和`local_intr_restore`的实现中可以很容易地分析出上面的结论：

    #define local_intr_save(x)      do { x = __intr_save(); } while (0)
    #define local_intr_restore(x)   __intr_restore(x);

    static inline bool
    __intr_save(void) {
        if (read_eflags() & FL_IF) {
            intr_disable();
            return 1;
        }
        return 0;
    }

    static inline void
    __intr_restore(bool flag) {
        if (flag) {
            intr_enable();
        }
    }

# 扩展实验：任意大小的内存分配算法

# 与参考答案的比较

## 分配并初始化进程控制块

在与参考实现比较的过程中，我没有发现重要的差异。参考实现比我的实现要长一些：

    proc->state = PROC_UNINIT;
    proc->pid = -1;
    proc->runs = 0;
    proc->kstack = 0;
    proc->need_resched = 0;
    proc->parent = NULL;
    proc->mm = NULL;
    memset(&(proc->context), 0, sizeof(struct context));
    proc->tf = NULL;
    proc->cr3 = boot_cr3;
    proc->flags = 0;
    memset(proc->name, 0, PROC_NAME_LEN);

我的实现中对应的部分是：

    memset(proc, 0, sizeof(struct proc_struct));
    proc->state = PROC_UNINIT;
    proc->parent = current;
    proc->cr3 = boot_cr3;

二者做的事情其实是一样的，除了我将`proc->parent`设置为了`current`，而参考实现是将其初始化为`NULL`，而这个细节差异在本实验中并不会导致任何后果，因为本实验中根本没有使用`proc->parent`。

## 为新创建的内核线程分配资源

这部分的主要差异包括：

* 参考实现使用了`local_intr_save`和`local_intr_restore`保证了操作的互斥性，而我的实现没有考虑这一点。这个差异如果延续到后面，就会成为我的实现上的一个比较重要的缺陷。不过，这个实验并不涉及抢占调度（到这门课程的很后面才有这部分的知识），实验指导书也没有提示我们考虑这一点，我在做这个任务的时候也就没有考虑这个的意识
* 参考实现考虑了更多边界异常情况，例如没有分配到页面等。这也是我做得不够完美的地方，以后我如果有机会写实用的系统，会尽量注意的

# 知识点

这个实验主要涉及了如下与原理课相对应的知识点：

* 进程控制块的数据结构。在实验中我们被要求对进程控制块进行初始化，目的应该就是让我们顺便熟悉一下进程控制块里面的字段。
* 进程的创建和初始化。在试验中我们对`do_fork`函数进行了补充，完成了一个原始的`fork`功能，这让我们了解了进程的创建和初始化过程中大致需要考虑哪些方面的问题，进行哪些操作。
* 进程的切换。这部分我们只是被要求看代码实现，大概是因为很多工作是用汇编完成的，自己实现工作量会有些大。我觉得这块其实是这个实验相关知识里面最绕的一个地方，因为包含了很多跳转，中间需要手动改变很多状态，而这些状态五花八门，设置的方式也不尽相同。另外，还需要考虑对用户程序进行一些包装，让它在运行结束的时候能够进行一些必要的收尾操作。

我感觉关于进程、进程调度的实验对知识点的覆盖挺完整的，想了一会儿没想到什么这个lab里没覆盖而且没有在之后的lab里也没有覆盖的知识点。毕竟四、五、六这三个lab对应的原理课就只有三讲。不像其他lab每个至少对应两讲原理课。
