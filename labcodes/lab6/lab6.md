# Round Robin算法

通过比较lab5和lab6关于进程调度部分的代码，我发现lab6在lab5的基础上主要做了如下修改：

* 进程调度的方式完全改变了。这从`schedule`函数可以看出。lab5中的调度是在从头到尾遍历进程队列的过程中一旦发现`RUNNABLE`状态的进程就尝试切换过去执行，而lab6中引入了执行队列和调度算法的`sched_struct`框架，调度的方式是调用调度算法来维护执行队列，借助调度算法决定下一个执行的进程。
* 由于引入了时间片的概念，lab6在时钟中断的处理中调用了`sched_class_proc_tick`，使调度算法能够正确维护时间片。

通过阅读Round Robin算法的实现，我了解了`sched_class`中各个函数指针的用法：

* `init`：传入参数为一个`run_queue`的指针。这个`run_queue`就是用于维护当前可以被调度算法调度运行的进程列表的数据结构。`init`函数对`run_queue`做了初始化工作，以便调度算法进行后面的调度工作。
* `enqueue`：传入参数为`run_queue`的指针以及`proc_struct`的指针。`enqueue`函数中，调度算法将指定的进程加入到指定的执行队列中。在调用`enqueue`后，指定的进程就已经存在于指定的执行队列中，可以在后续操作中被调度算法调度了。
* `dequeue`：传入参数为`run_queue`的指针以及`proc_struct`的指针。`dequeue`函数中，调度算法将制定的进程从指定的执行队列中移除，使指定进程在后续操作中不再能被调度算法调度。
* `pick_next`：传入参数为`run_queue`的指针。在这个函数中，调度算法从指定执行队列中选择下一个执行的进程。
* `proc_tick`：传入参数为`run_queue`的指针以及`proc_struct`的指针。调用这个函数就是告诉调度算法某个进程又运行了1个tick的时间，调度算法可能会利用这个信息进行抢占调度，即在发现这个进程执行足够久时将其控制块的`need_resched`字段置为1。

ucore的调度执行过程大致如下：

* 调度的触发。在ucore中，有以下几个地方会触发调度：
    * 在进程希望主动交出执行权时。这包括了`idleproc`的日常工作，以及exit和wait系统调用。`idleproc`执行时会反复执行调度以尽快交出执行权（`idleproc`本身啥事也干不了）：

        void
        cpu_idle(void) {
            while (1) {
                if (current->need_resched) {
                    schedule();
                }
            }
        }
    
        exit和wait系统调用也都会调用`schedule`函数。

        另外，yield系统调用也是如此。在`do_yield`函数中`need_resched`被置为1，而在用户态产生的中断处理的最后，会调用`schedule`进行调度：


            if (!in_kernel) {
                if (current->flags & PF_EXITING) {
                    do_exit(-E_KILLED);
                }
                if (current->need_resched) {
                    schedule();
                }
            }

    * 调度算法触发的调度。这包括调度算法发现当前执行的进程为`idleproc`和当前执行的进程时间片已经结束两种情况：

        if (proc != idleproc) {
            sched_class->proc_tick(rq, proc);
        }
        else {
            // cprintf("NEED RESCHEDULE! %d\n", proc == idleproc);
            proc->need_resched = 1; // we do not need to wait for a time slice to end, 
                // since idle_proc does nothing
        }

        `proc_tick`中：

        if(proc->time_slice == 0){
            proc->need_resched = 1;
        }
    
        同样，这会导致在用户态产生的中断处理的最后调用`schedule`进行调度。

进行调度时，内核会调用调度算法的`pick_next`函数获取下一个执行的进程。当下一个执行的进程不是当前运行的进程时，便会进行进程切换：

    if ((next = sched_class_pick_next()) != NULL) {
        sched_class_dequeue(next);
    }
    if (next == NULL) {
        next = idleproc;
    }
    next->runs ++;
    if (next != current) {
        proc_run(next);
    }

至于调度算法的`enqueue`函数，它会在两种情况下被调用：

* 进程从`RUNNING`（实际上没有这个标识，但是逻辑上是存在的）状态进入`RUNNABLE`状态时。这对应`schedule`函数内的

    if (current->state == PROC_RUNNABLE) {
        sched_class_enqueue(current);
    }

* 进程从`SLEEPING`或`UNINIT`状态进入`RUNNABLE`状态时，这对应`wakeup_proc`函数：

    if (proc->state != PROC_RUNNABLE) {
        proc->state = PROC_RUNNABLE;
        proc->wait_state = 0;
        if (proc != current) {
            sched_class_enqueue(proc);
        }
    }

请在实验报告中简要说明如何设计实现”多级反馈队列调度算法“，给出概要设计，鼓励给出详细设计？

为了实现“多级反馈队列调度算法”，需要维护K个独立的进程队列，并且额外记录当前执行的进程（`current`）所在的进程队列的编号（其实可以通过`proc->rq`获得）。在时间片结束触发的调度中将`current`进程添加到自己原来所属的队列的下一个队列中（即如果原来属于队列i，则将其添加至队列(i+1)）。每一次调度时，从队列1开始，往后找到第一个非空的队列，使用FCFS策略取出其中的一个进程执行。


# Stride Scheduling算法

我的实现完全使用了`sched_class`的接口。因此，实现的过程对应了对其中各个函数的实现。

## `init`

由于Stride Scheduling使用的具体的执行队列可以为普通的线性队列，也可以为堆（更加高效），这里的实现和Round Robin的区别就是根据`USE_SKEW_HEAP`宏选择性地对二者进行初始化。对线性队列的初始化与Round Robin完全一致，对堆的初始化则直接是将`lab6_run_pool`置为`NULL`。

## `enqueue`

同样地，这个函数也需要根据`USE_SKEW_HEAP`宏提供两种不同的实现。

如果使用线性队列，则用`list_add_before`函数将进程添加到队列中。如果使用堆，则是`skew_heap_insert`函数。值得一提的是，由于堆结构需要指定一个全序关系，`skew_heap_insert`函数有一个参数为元素的比较函数。这里我使用的比较函数就是`proc_stride_comp_f`，它比较的是`lab6_stride`这一字段，也就是Stride调度算法在每次挑选下一个执行的进程时考虑的指标。

除此之外，`enqueue`函数还对进程时间片内剩余的时间以及所在的执行队列进行了初始的设置：

      proc->time_slice = rq->max_time_slice;
      proc->rq = rq;
      

## `dequeue`

对于线性队列，调用`list_del_init`函数从队列中移除进程。对于堆，使用`skew_heap_remove`函数进行移除。

此外，`dequeue`函数将`proc->rq`重置为`NULL`，表示该进程不再属于任何执行队列。

## `pick_next`

对于线性队列，`pick_next`遍历了整个执行队列，从中找出`lab6_stride`最小的进程作为结果返回。对于堆，直接返回堆顶的元素`le2proc(rq->lab6_run_pool, lab6_run_pool)`。从这里可以看出使用堆维护执行队列是更加高效的选择。

此外，由于`pick_next`返回的进程即将获得一次执行的机会，`pick_next`根据这个进程的优先级更新了它的`lab6_stride`字段：

    if(next_proc->lab6_priority == 0)
        next_proc->lab6_stride += BIG_STRIDE;
    else
        next_proc->lab6_stride += BIG_STRIDE / next_proc->lab6_priority;

## `proc_tick`

这部分与Round Robin算法完全一致，因为二者都是基于时间片的。具体的做法就是，将`proc->timeslice`减一，发现其为0时，说明时间片已经结束，故置`proc->need_resched`为1。

# 扩展：CFS调度算法

# 与参考实现的比较

我发现我的实现与参考实现有一处差异值得一提。下面我对这处差异进行详细的讨论。

在`enqueue`函数中，参考是实现是这样设置进程的`time_slice`的：

    if (proc->time_slice == 0 || proc->time_slice > rq->max_time_slice) {
        proc->time_slice = rq->max_time_slice;
    }

而我的设置方式是直接`proc->time_slice = rq->max_time_slice;`。

这种差异会导致什么后果呢？我分析了一下，参考实现想要达到的效果其实就是使原来在1到`rq->max_time_slice`之间的`proc->time_slice`的值保持不变。出现这种情况，应该只可能是进程yield或是wait之后被重新调度这种情况（总之，是进程的时间片没用完就交出了执行权，之后又重获执行权）。我的实现相当于允许进程通过yield或wait，在再次获得执行权的时候能够将时间片续满，而参考实现相当于禁止这种操作。乍看去似乎我的实现是有什么漏洞允许进程无限制地续时间片，从而霸占执行权，但是经过仔细的思考，我发现禁止这种操作其实没有什么道理，因为每次进程通过这种操作续满时间片，它都是以一个完整的执行次数为代价的。以Stride调度算法为例，每次调度，其实调度算法不管时间片有没有用完，都会更新`stride`字段。这些通过yield或wait主动让出执行权的进程，被多更新了一次`stride`字段，而获得的额外的执行时间却比一个完整的时间片要少，相比那些时间片用完被动放弃执行权的进程，它们其实是亏了。

# 知识点

本次实验中涉及的重要知识点包括：

* 内核抢占点：通过阅读代码，我大致理清楚了调度算法会在哪些情况下被调用，以及这样设计产生的效果。
* 调度算法：在试验中我阅读了Round Robin的实现，并且自己动手实现了Stride Scheduling。通过这个实验，我对调度算法的作用、完成的功能以及实现方式有了较深的理解。

我认为比较重要但是实验未覆盖的内容包括：

* 各种调度算法的比较：类似于前面关于置换算法的实验，我认为这个知识点可以通过更高层的模拟实验进行覆盖，因为在内核中直接进行调试和研究会有诸多不便之处，且调度算法本身几乎不依赖什么和底层硬件相关的特性。
