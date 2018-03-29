# 加载应用程序并执行

在`load_icode`函数中我对进程的trapframe进行了如下的设置：

    tf->tf_cs = USER_CS;
    tf->tf_ds = tf->tf_es = tf->tf_ss = USER_DS;
    tf->tf_esp = USTACKTOP;
    tf->tf_eip = elf->e_entry;
    tf->tf_eflags |= FL_IF;

由于`load_icode`加载了用户程序，并将用户程序的内容作为当前进程的内容，在中断返回时应该切换到用户态，并且跳转到程序的入口地址开始执行。上面的前三行就是设置中断返回时恢复的段寄存器和栈指针的值，第三行设置中断返回后跳转到的指令的地址，最后一行使能了继续执行该进程时的中断。

值得一提的是，这里的`tf`与`current->tf`是一样的。我们都知道`current->tf`本身只是进程控制块里面的一个字段，那么，这个`tf`为什么又能够在中断返回时产生作用呢？通过阅读代码，我发现`trapentry.S`构造了位于内核栈的`trapframe`，并且会在`iret`前从该`trapframe`进行现场的恢复，最终`iret`时栈顶也指向这个`trapframe`的内容。因此，这个`trapframe`的内容决定了中断返回后的状态。我发现，`trapentry.S`将这个`trapframe`传给了`trap.c`中的`trap`函数（参数名为`tf`），而这个`trap`函数进行了如下操作：

    struct trapframe *otf = current->tf;
    current->tf = tf; // interesting

    bool in_kernel = trap_in_kernel(tf);

    trap_dispatch(tf);
    
    current->tf = otf;

可以看到，`trap`函数将`current->tf`设为了`tf`。也就是说，在`trap_dispatch`中对`current->tf`进行的修改，事实上就是对`trapentry.S`中构造的`trapframe`的修改，也就能够在中断返回时起作用。

总结而言，用户进程从创建到正式开始执行的流程大体如下：

* 内核创建内核线程（例如实验中的`user_main`）后，将其置为`RUNNABLE`状态
* `idleproc`在从进程列表中选择该内核线程，切换到该内核线程执行（直到这里都是上一个lab的内容）
* 在该内核线程中，发出`exec`系统调用，最终会跳转到`do_execve`执行
* 在`do_execve`中，内核为用户程序的执行各种初始化工作，例如创建页表、分配页面、加载代码到内存空间等。这时，这个内核线程成为了用户进程，拥有了自己独立的内存管理信息
* 内核修改了中断的`trapframe`，在中断返回时会进入用户态，并跳转到程序的入口处执行

# 父进程复制自己的内存空间给子进程

`copy_range`这个函数干的事情就是将一个页表上线性地址连续的若干页帧进行拷贝，并将另一个页表上同样的线性地址空间映射到对应的页帧副本上。实现非常直接，感觉可说的不多：

    uintptr_t src_addr = page2pa(page),
        dst_addr = page2pa(npage);
    memcpy(KADDR(dst_addr), KADDR(src_addr), PGSIZE);
    page_insert(to, npage, start, perm);

其中，`page`是要进行拷贝的单个页帧（的`Page`信息），`npage`是新创建的用于放置副本的页帧，`src_addr`和`dst_addr`是两个页帧的起始物理地址。在用`memcpy`函数进行拷贝时，需要用`KADDR`将这两个物理地址转换为内核虚拟地址，因为有分段机制，即虽然内核中线性地址与物理地址相同，逻辑地址与线性地址仍有一个偏移量的差别。

为了实现Copy on Write机制，在`fork`中无需将父进程的内存空间复制给子进程，只需要拷贝父进程的页表给子进程，然后将这些页面全部设置为只读。为了在进程Write的时候能够进行页面的拷贝，页访问异常的服务例程中需要加入相应的处理。

# fork/exec/wait/exit 的实现，以及系统调用的实现

通过阅读代码（`do_fork`、`do_execve`、`do_wait`、`do_exit`），可以发现这几个系统调用做的事情大致分别如下：

* fork：拷贝一份当前运行的进程，其运行的上下文与当前进程几乎完全一样。值得一提的一点差异在`copy_thread`函数中可以看到：

        static void
        copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf) {
            proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
            *(proc->tf) = *tf;
            proc->tf->tf_regs.reg_eax = 0;
            proc->tf->tf_esp = esp;
            proc->tf->tf_eflags |= FL_IF;

            proc->context.eip = (uintptr_t)forkret;
            proc->context.esp = (uintptr_t)(proc->tf);
        }

    可以看到，新进程`trapframe`中保存的`eax`被改为了0。而在`syscall`函数中，中断的`trapframe`中保存的`eax`被这样设置：

        tf->tf_regs.reg_eax = syscalls[num](arg);

    对于fork来说，它返回的是新进程的pid。可以看到，系统调用的调用者得到的返回值为新进程的pid，而新进程得到的返回值是0。这种处理使程序能够通过fork系统调用的返回值来判断自己是父进程还是子进程，并且如果是父进程的话可以获知子进程的pid。

    fork并不会直接影响调用者的执行状态。它只创建一个新进程，然后返回到调用者继续执行，新进程需要在进程队列中等待内核的调度。对于新进程来说，它从`UNINIT`状态转变为了`RUNNABLE`状态。

* exec：上面已经讲得比较充分了，感觉不需要再多讲了。exec也不会改变进程的执行状态，它完成一些操作后会返回到调用者。
* wait：检查一个特定的子进程（通过pid标识）或任意一个子进程（参数pid=0）的状态是否为`ZOMBIE`。如果有这样的子进程，将其内核栈释放，从进程哈希表和进程列表中移除，并将其返回码放在`ecx`寄存器中作为系统调用返回值。否则，通过如下语句将进程改为等待子进程的状态：

        current->state = PROC_SLEEPING;
        current->wait_state = WT_CHILD;

    再调用`schedule`函数将执行权交出。可以想见，由于进程状态不再是`RUNNABLE`，一旦执行权被交出，在exit系统调用中发现它的`ZOMBIE`状态的子进程之前，这个进程将不会再次得到执行权。

    可以发现，wait系统调用可以将调用者的状态改变为`SLEEPING`，也可以将某个`ZOMBIE`状态的子进程回收释放。
* exit：大概做了下面几件事
    * 释放内存空间（只对用户进程做）
    * 在进程控制块中保存进程的返回码
    * 将进程置为`ZOMBIE`状态（等待父进程进行回收处理）
    * 检查父进程是否处于等待子进程的状态，如果是这样，将其唤醒
    * 将进程的所有子进程托管给`initproc`处理（将它们改成`initproc`的子进程）
    * 检查`initproc`是否处于等待子进程的状态，如果是这样，并且有子进程为`ZOMBIE`状态，则将`initproc`唤醒

    可以看见，exit系统调用会将进程的执行状态由`RUNNABLE`改变为`ZOMBIE`。

用户态进程生命周期图大致如下：


                                               _[exec]__
                                              |         |
                                              v         |
    --[fork]--> RUNNABLE --(schedule)--> (RUNNING) -----
                   ^                          |         |
                   |                        [wait]    [exit]
                 等待条件满足                   |         |
                   ---------- SLEEPING <-------         v
                                                      ZOMBIE
                                                        |
                                                    被父进程回收
                                                        |
                                                        v
                                                      进程结束


`UNINIT`状态只在`fork`过程中存在，因此没有在图中画出。代码并没有直接体现`RUNNNING`状态，而是通过`current`记录当前正在执行的进程来实现。

# 扩展：Copy on Write

# 与参考答案的比较

## 加载应用程序并执行

这一练习中我的实现与参考实现没有任何区别。

## 父进程复制自己的内存空间给子进程

我的实现与参考实现间没有重要的区别。参考实现比我的要短小一些，因为他直接使用了`page2ka`函数从`Page`获得其对应的内核虚拟地址，而我的实现是先用`page2pa`函数获取`Page`对应的物理地址，再使用`KADDR`将物理地址转换为内核虚拟地址。本质上，这两种实现没有任何区别，因为`page2ka`的实现方式也是先得到物理地址再用`KADDR`进行转换。

# 知识点

这次实验中涉及原理课中的如下重要知识点：

* 进程状态。直接与进程状态相关的编码任务似乎没有，但是在第三个任务中我通过阅读代码较好地理解了进程状态的转换在何时以及如何发生，这些状态信息又是在哪些地方被加以利用的。
* 应用程序的加载/用户进程的创建。这一块对应过来是lab里的exec系统调用。我通过自己填入一些代码以及阅读附近的相关代码对操作系统如何加载应用程序、进程如何正式开始执行所加载的应用程序都有了较深入的理解。
* 进程的等待与退出。这对应了lab里的任务三。我通过阅读代码了解了进程运行结束后如何进入`ZOMBIE`状态然后被父进程回收掉，以及父进程如何通过wait系统调用等待子进程进入`ZOMBIE`状态，等待条件发生变化（请注意不一定是满足，例如即使父进程在等待子进程退出的状态下由于有被发现的`ZOMBIE`状态子进程而被唤醒，这个`ZOMBIE`子进程可能也并不是父进程在等待退出的那个子进程）时又如何被唤醒。
* 进程切换。这个知识点十分基础，上个lab已经有所涉及，只是当时情形十分简单，而现在稍微复杂一点。在这个lab中，进程切换可能发生在：
    * 进程发出exit系统调用主动退出，进入`ZOMBIE`状态后
    * 进程发出wait系统调用进入`SLEEPING`状态后
    * `idleproc`运行时
    
    上个lab中进程切换只可能发生在第三种情况。
