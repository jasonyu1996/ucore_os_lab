# 练习一

## ucore.img生成过程

通过观察make "V="的输出以及Makefile文件的内容，我总结出生成ucore.img的过程如下：

1. 生成kernel
    1. 编译源文件（包括c和汇编），生成对象文件，例如   

        ```
            i386-elf-gcc -Ikern/trap/ -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -Ilibs/ -Ikern/debug/ -Ikern/driver/ -Ikern/trap/ -Ikern/mm/ -c kern/trap/vectors.S -o obj/kern/trap/vectors.o
        ```

        ```
            i386-elf-gcc -Ikern/debug/ -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -Ilibs/ -Ikern/debug/ -Ikern/driver/ -Ikern/trap/ -Ikern/mm/ -c kern/debug/panic.c -o obj/kern/debug/panic.o
        ```

        参数说明：
            
        * -I指定了头文件的位置
        * -fno-builtin禁止编译器将以__builtin__开头的函数之外的函数识别为内建函数（即编译器直接提供了支持的需要特殊对待的函数）
        * -ggdb要求编译器生成GDB专用的调试信息
        * -nostdinc要求编译器忽视标准库头文件
        * -fno-stack-protector禁止编译器应用栈保护机制。如果不加上这个标记编译器可能会在栈中加入一些额外的数据来进行一定程度的栈保护（检测栈缓冲区溢出），而这会影响栈布局的预测和控制

    2. 链接对象文件

        ```
        i386-elf-ld -m    elf_i386 -nostdlib -T tools/kernel.ld -o bin/kernel  obj/kern/init/init.o obj/kern/libs/readline.o obj/kern/libs/stdio.o obj/kern/debug/kdebug.o obj/kern/debug/kmonitor.o obj/kern/debug/panic.o obj/kern/driver/clock.o obj/kern/driver/console.o obj/kern/driver/intr.o obj/kern/driver/picirq.o obj/kern/trap/trap.o obj/kern/trap/trapentry.o obj/kern/trap/vectors.o obj/kern/mm/pmm.o  obj/libs/printfmt.o obj/libs/string.o
        ```

        参数说明：

        * -m elf_i386：指定链接目标为i386 ELF格式
        * -nostdlib要求链接器忽略标准库目录，仅在命令行给出的位置查找需要的库进行链接
        * -T指定链接脚本。这个链接脚本会替代连接器自导的默认链接脚本。粗略研究了一下链接脚本tools/kernel.ld，我发现它主要是指定了链接过程中每部分数据的组织顺序和方式等，例如`. = 0x100000;`指定了kernel在内存中的起始地址为0x100000

2. 生成bootblock.o
    1. 编译源文件bootasm.S和bootmain.c，分别生成bootasm.o和bootmain.o：

        ```
            i386-elf-gcc -Iboot/ -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -Ilibs/ -Os -nostdinc -c boot/bootasm.S -o obj/boot/bootasm.o
        ```
        
        ```
            i386-elf-gcc -Iboot/ -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -Ilibs/ -Os -nostdinc -c boot/bootmain.c -o obj/boot/bootmain.o
        ```

        参数说明：

        * -m32指定编译器生成32位字长i386的代码
        * -gstabs指定生成stabs格式的调试信息（不带GDB扩展）
        * -Os开启编译器对代码长度的优化。在编译bootloader时开启这个优化是因为整个bootloader必须能够放入硬盘的一个扇区中，长度限制比较紧


    2. 链接bootasm.o、bootmain.o生成bootblock.o

        ```
        i386-elf-ld -m    elf_i386 -nostdlib -N -e start -Ttext 0x7C00 obj/boot/bootasm.o obj/boot/bootmain.o -o obj/bootblock.o
        ```

        参数说明：

        * -N指定链接器不以也大小对齐数据段
        * -e start 指定目标文件的入口地址为符号start（在bootasm.S文件中定义）
        * -Ttext 0x7C00 要求链接器进行重定位，将代码段放置于0x7c00位置（即BIOS初始化完成后跳转到的位置）

3. 使用objcopy及sign工具生成bootblock：它们做的事情大概是将bootblock.o转为裸的二进制代码，然后再进行适当的包装（如加入0x55aa标记），生成BIOS可以识别的硬盘主引导扇区（放在了bin/bootblock文件中）

    ```
	@$(OBJCOPY) -S -O binary $(call objfile,bootblock) $(call outfile,bootblock)
	@$(call totarget,sign) $(call outfile,bootblock) $(bootblock)
    ```

4. 合成镜像文件ucore.img
    1. 用零填充10000个扇区，创建ucore.img（或将现有的文件清空）

        ```
        dd if=/dev/zero of=bin/ucore.img count=10000
        ```
    2. 将bootloader写入第一个扇区

        ```
        dd if=bin/bootblock of=bin/ucore.img conv=notrunc
        ```

        这里conv=notrunc表示覆盖镜像上重叠位置原有的数据
    
    3. 从第二个扇区开始将kernel写入镜像

        ```
            dd if=bin/kernel of=bin/ucore.img seek=1 conv=notrunc
        ```

        这里seek=1即要求dd将初始访问位置设置为1号扇区（第二个扇区）

## 符合规范的硬盘主引导扇区特征

符合规范的硬盘主引导扇区的最高两个字节应该是0x55aa，这从sign.c中可以可看出：

    buf[510] = 0x55;
    buf[511] = 0xAA;

# 练习二

* 可以发现，第一条指令位于0x0000fff0

    ```
    (gdb) target remote localhost:1234
        Remote debugging using localhost:1234
        0x0000fff0 in ?? ()
    ```
* 可以看出x86指令是变长的

    ```
    0x0000e066 in ?? ()
    (gdb)
    0x0000e068 in ?? ()
    (gdb)
    0x0000e06a in ?? ()
    (gdb)
    0x0000e070 in ?? ()
    (gdb)
    0x0000e076 in ?? ()
    (gdb)
    ```
* 在0x7c00（bootloader第一条指令）处设置断点

    ```
    (gdb) b *0x7c00
    Breakpoint 1 at 0x7c00
    (gdb) c
    Continuing.
    => 0x7c00:      cli

    Breakpoint 1, 0x00007c00 in ?? ()
    (gdb)
    ```

    可以看到，bootloader的第一条指令为cli。

* 继续bootloader的执行

    ```
    (gdb) si
    => 0x7c01:      cld
    0x00007c01 in ?? ()
    (gdb)
    => 0x7c02:      xor    %eax,%eax
    0x00007c02 in ?? ()
    (gdb)
    => 0x7c04:      mov    %eax,%ds
    0x00007c04 in ?? ()
    (gdb)
    => 0x7c06:      mov    %eax,%es
    0x00007c06 in ?? ()
    (gdb)
    => 0x7c08:      mov    %eax,%ss
    0x00007c08 in ?? ()
    (gdb)
    => 0x7c0e:      jne    0x7c0a
    0x00007c0e in ?? ()
    (gdb)
    => 0x7c10:      mov    $0xd1,%al
    0x00007c10 in ?? ()
    (gdb)
    ```
    
    通过比较可以发现，此处jne的目标位置已经在链接的时候填入了具体的地址，在bootblock.asm中已经可以看到`jne 0x7c0a`。

* 设置在内核第一条指令的断点

    ```
    (gdb) b *0x100000
    Breakpoint 2 at 0x100000
    (gdb) c
    Continuing.
    => 0x100000:    push   %ebp

    Breakpoint 2, 0x00100000 in ?? ()
    (gdb) si
    => 0x100001:    mov    %esp,%ebp
    0x00100001 in ?? ()
    (gdb)
    => 0x100003:    sub    $0x18,%esp
    0x00100003 in ?? ()
    (gdb)
    => 0x100006:    mov    $0x10fd80,%edx
    0x00100006 in ?? ()
    (gdb)
    => 0x10000b:    mov    $0x10ea18,%eax
    0x0010000b in ?? ()
    (gdb)
    => 0x100010:    sub    %eax,%edx
    0x00100010 in ?? ()
    (gdb)
    ```

    比较kernel.asm内容，可以发现系统正在执行内核初始化部分代码（kern_init）：

    ```
    int
    kern_init(void) {
  100000:	55                   	push   %ebp
  100001:	89 e5                	mov    %esp,%ebp
  100003:	83 ec 18             	sub    $0x18,%esp
    extern char edata[], end[];
    memset(edata, 0, end - edata);
  100006:	ba 80 fd 10 00       	mov    $0x10fd80,%edx
  10000b:	b8 18 ea 10 00       	mov    $0x10ea18,%eax
  ```
# 练习三

## bootloader由实模式进入保护模式的方式

### 开启A20

为了开启A20地址线，bootloader需要向8042键盘控制器发送命令更改其端口P2的P21引脚的输出（由0置为1）。

具体地，bootloader需要通过对IO端口0x60及0x64的输入输出操作来与8042键盘控制器进行交互。

首先，在向8042键盘控制器IO写入数据前，要确保其输入缓冲为空，即等待其状态寄存器的位1变为0，而状态寄存器可以通过从0x64端口读入来获得：

    seta20.1:
    inb $0x64, %al                        
    testb $0x2, %al
    jnz seta20.1

然后向0x64端口写入相应的命令：
    
    movb $0xd1, %al                                
    outb %al, $0x64

这里0xd1就对应设置P2端口的命令。

然后，bootloader需要向8042键盘控制器发送要设置的值。这具体通过向0x60端口输出数据实现（同样地，需要先确保键盘控制器输入缓冲为空）

    seta20.2:
    inb $0x64, %al 
    testb $0x2, %al
    jnz seta20.2

    movb $0xdf, %al
    outb %al, $0x60  

这里，0xdf就是P2设置后的值。可以看到，bootloader将P2端口所有引脚的值都置为了1，其中包括A20地址线的使能位。

### 初始化GDT

在进入保护模式前，bootloader需要设置好GDT。

首先，bootloader中GDT如下：

    .p2align 2                            
    gdt:
    SEG_NULLASM
    SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff) 
    SEG_ASM(STA_W, 0x0, 0xffffffff)

    gdtdesc:
    .word 0x17
    .long gdt       

这里gdt是GDT的基地址。这个GDT包含了三个段描述符：空描述符（为了让选择GDT中0号段的段选择子成为保留的空段选择子），基地址为0、长度为4GB的代码段（可读可执行），基地址为0、长度为4GB的数据段（可读写）。

gdtdesc为设置GDTR内容的数据的起始地址。这段数据长6个字节，低两字节为0x17，及GDT尾部相对于起始地址偏移量（长度-1），高四字节为GDT起始地址。

最后，bootloader通过

    lgdt gdtdesc

设置GDTR，完成了GDT的初始化。

### 使能和进入保护模式

bootloader在初始化GDT后，设置CR0中保护模式的使能位为1：

    movl %cr0, %eax
    orl $CR0_PE_ON, %eax
    movl %eax, %cr0

其中CR0_PE_ON=0x1。

为了在重置系统，重设CS段寄存器，以真正进入32位保护模式，bootloader使用了一个far jump指令：

    ljmp $PROT_MODE_CSEG, $protcseg

    .code32
    protcseg:

其中ljmp的第一个参数PROT_MODE_CSEG即为far jump设置的CS段寄存器值。可以看到PROT_MODE_CSEG=0x8，也就是GDT中的1号段的段选择子，且RPL设置为0。


# 练习四

## bootloader读取硬盘扇区的方式

bootloader通过设置硬盘IO寄存器实现硬盘的读取（PIO方式）。

我们这个bootloader只读第一个IDE通道，其对应的IO寄存器为0x1f0~0x1f7。

在读取扇区时，bootloader首先从0x1f7读取状态寄存器进行判断，等待硬盘空闲：

    /* waitdisk - wait for disk ready */
    static void
    waitdisk(void) {
        while ((inb(0x1F7) & 0xC0) != 0x40)
            /* do nothing */;
    }

然后在0x1f2~0x1f6中设置扇区号：

    outb(0x1F2, 1);                         // count = 1
    outb(0x1F3, secno & 0xFF);
    outb(0x1F4, (secno >> 8) & 0xFF);
    outb(0x1F5, (secno >> 16) & 0xFF);
    outb(0x1F6, ((secno >> 24) & 0xF) | 0xE0);

再写入0x1f7，设置命令寄存器为读取扇区命令（0x20）:

    outb(0x1F7, 0x20);                      // cmd 0x20 - read sectors

再次调用waitdisk等待硬盘执行命令将数据准备就绪。

最后，bootloader通过insl指令从0x1f0读取扇区数据并写到指定的内存地址：
    
    asm volatile (
            "cld;"
            "repne; insl;"
            : "=D" (addr), "=c" (cnt)
            : "d" (port), "0" (addr), "1" (cnt)
            : "memory", "cc");


## bootloader加载ELF格式

bootloader从硬盘读取了8个扇区（一个页）的数据放置于以0x10000为起始地址的连续内存空间中：

    readseg((uintptr_t)ELFHDR, SECTSIZE * 8, 0);

之后，bootloader会检查ELF头部的magic number已确认读入的数据为合法的ELF头：

    if (ELFHDR->e_magic != ELF_MAGIC) {
        goto bad;
    }

bootloader接下来读取ELF的程序头，从硬盘读入各个段的数据以构建可供运行的内存镜像：

    ph = (struct proghdr *)((uintptr_t)ELFHDR + ELFHDR->e_phoff);
    eph = ph + ELFHDR->e_phnum;
    for (; ph < eph; ph ++) {
        readseg(ph->p_va & 0xFFFFFF, ph->p_memsz, ph->p_offset);
    }

将各段的数据装入内存后，bootloader将PC指向进程入口地址ELFHDR->e_entry。此操作将控制权正式交给了OS kernel。

    ((void (*)(void))(ELFHDR->e_entry & 0xFFFFFF))();

# 练习五

函数调用堆栈布局如下所示（取自实验指导书）：

    +|  栈底方向        | 高位地址
    |    ...        |
    |    ...        |
    |  参数3        |
    |  参数2        |
    |  参数1        |
    |  返回地址        |
    |  上一层[ebp]    | <-------- [ebp]
    |  局部变量        |  低位地址

为了在print_stackframe中打印函数调用堆栈信息，我调用了read_ebp、read_eip函数获取ebp寄存器和eip寄存器的值，然后按照上述的堆栈布局输出返回地址以及参数。

	uint32_t ebp = read_ebp(), eip = read_eip();
	int i;
	do{
		cprintf("ebp=0x%08x, eip=0x%08x\n", ebp, eip);
		cprintf("\tArgs:\n");
		for(i = 0; i < 4; i ++)
			cprintf("\t\t0x%08x\n", *((uint32_t*)ebp + 2 + i));
		print_debuginfo(eip - 1);
		cprintf("\n");
		eip = *((uint32_t*)ebp + 1);
		ebp = *((uint32_t*)ebp);
	} while(ebp != 0);	

输出如下：

    ebp=0x00007b38, eip=0x0010097a
            Args:
                    0x00010074
                    0x00010074
                    0x00007b68
                    0x00100084
        kern/debug/kdebug.c:305: print_stackframe+21

    ebp=0x00007b48, eip=0x00100c5f
            Args:
                    0x00000000
                    0x00000000
                    0x00000000
                    0x00007bb8
        kern/debug/kmonitor.c:125: mon_backtrace+10

    ebp=0x00007b68, eip=0x00100084
            Args:
                    0x00000000
                    0x00007b90
                    0xffff0000
                    0x00007b94
        kern/init/init.c:48: grade_backtrace2+19

    ebp=0x00007b88, eip=0x001000a6
            Args:
                    0x00000000
                    0xffff0000
                    0x00007bb4
                    0x00000029
        kern/init/init.c:53: grade_backtrace1+27

    ebp=0x00007ba8, eip=0x001000c3
            Args:
                    0x00000000
                    0x00100000
                    0xffff0000
                    0x00100043
        kern/init/init.c:58: grade_backtrace0+19

    ebp=0x00007bc8, eip=0x001000e4
            Args:
                    0x00000000
                    0x00000000
                    0x0010fd80
                    0x001034e0
        kern/init/init.c:63: grade_backtrace+26

    ebp=0x00007be8, eip=0x00100050
            Args:
                    0x00000000
                    0x00000000
                    0x00000000
                    0x00007c4f
        kern/init/init.c:28: kern_init+79

    ebp=0x00007bf8, eip=0x00007d70
            Args:
                    0xc031fcfa
                    0xc08ed88e
                    0x64e4d08e
                    0xfa7502a8
        <unknow>: -- 0x00007d6f --

每一次调用print_stack的输出中，第一行为ebp和eip寄存器的值，Args下的四行为函数调用的参数，最后一行为当前位置的函数名以及函数内的偏移量。

# 练习六

## 中断描述符表项

中断描述符表项长度为8个字节。其中处理程序入口为：第16位~第31位的段选择子+高16位和低16位拼接而成的偏移量。

## IDT初始化

IDT表项中的中断处理程序入口逻辑地址全部保存在vector.S中__vectors位置开始的连续空间中，每一项占两个字（4个字节）。

trap.c中，内核对IDT进行了初始化。整个IDT的内容存储在idt中：

    static struct gatedesc idt[256] = {{0}};

在idt_init函数内，内核向idt内填充IDT表项。表项的设置过程使用了SETGATE宏。SETGATE会根据给定的参数设置好IDT表项的内容：

    int i;
	for(i = 0; i < 256; i ++){
		SETGATE(idt[i], i == T_SYSCALL || i == T_SWITCH_TOK ||
            i == T_SWITCH_TOU, GD_KTEXT, __vectors[i], (i == T_SYSCALL
             || i == T_SWITCH_TOK) ? 3 : 0);
	}

## 时钟中断

为了让内核每100次时钟中断后调用print_ticks函数向屏幕打印信息，我在trap.c的trap_dispatch函数中修改了对IRQ_OFFSET + IRQ_TIMER类型中断的处理：

    ++ ticks;
    if(ticks == TICK_NUM){
        print_ticks();
        ticks = 0;	
    }

其中ticks是一个用于计响应记录时钟中断数量的变量，TICK_NUM=100。每当ticks增长到TICK_NUM时，就调用print_ticks并将ticks重置。

# 扩展练习


## 用户态到内核态

讲道理的话，操作系统允许应用程序通过系统调用就能实现从用户态到内核态的转换，其实是非常不科学的一件事。不过，这个练习可能也只是一个练习，并不会在实际的操作系统设计中出现。

为了实现这个功能，需要利用中断返回过程的特点。关键在于，系统当前的特权级体现在段寄存器的内容上，CS段寄存器并不能直接像普通的寄存器那样写入，但是可以通过iret指令从栈中恢复段寄存器的特点来进行设置。于是，我所做的就是巧妙地构造出栈，使得iret指令能够让程序继续执行，但是段寄存器的值已被另外修改。

        if(tf->tf_cs != KERNEL_CS){
            tf->tf_cs = KERNEL_CS;
            tf->tf_ds = tf->tf_es = KERNEL_DS;
            tf->tf_eflags &= ~0x3000;
        }


x86中断产生时，硬件会向栈中压入：err、cs、eip、eflags，当中断涉及特权级变化（即在用户态产生中断）时，硬件会在压入上列寄存器之前先压入esp和ss。除了修改栈中保存的段寄存器，我还需要考虑esp和ss在什么时候应该删去，什么时候应该额外补上。

这里由于系统调用是从用户态陷入了内核态，栈中会保存有esp和ss寄存器，但是在修改了cs寄存器后，硬件通过对其进行判断，发现是从内核态返回到内核态，便不会从栈中取出esp和ss，故系统调用返回后栈顶会多出esp和ss，这时我们不希望发生的。解决办法非常简单，只需要在系统调用后调整栈顶位置，将栈复原即可。

    static void
    lab1_switch_to_kernel(void) {
        //LAB1 CHALLENGE 1 :  TODO
        asm volatile (
            "int %0 \n"
            "movl %%ebp, %%esp \n"
            : 
            : "i"(T_SWITCH_TOK)
        );
    }

值得一提的是，这里为了将栈复原，我直接将ebp的值赋给了esp。这是由于lab1_switch_to_kernel是一个函数，且这个函数的栈帧始终为空，因此我们可以确定在进行特权级切换前esp与ebp的值相等。

## 内核态到用户态

从内核态转变到用户态的做法是类似的，只是处理esp和ss的时候有些区别。在这种情况下，系统调用是在内核态发生的，没有特权级的变化，因而硬件不会压入esp和ss，但在系统调用返回时，由于中间cs寄存器发生了变化，返回时存在特权级变化，硬件会从栈中取出esp和ss。为了让系统调用返回时硬件能够取出正常的esp和ss，保持栈与进行特权级转换操作前相同，我在进行系统调用前先手动在栈中留出了esp和ss的空间：

    asm volatile (
        "sub $0x8, %%esp;"
        "int %0;"
        "movl %%ebp, %%esp;"
        :
        : "i"(T_SWITCH_TOU)
    );

    if(tf->tf_cs != USER_CS){
        asm volatile (
            "addl 8, %esp;"
        );
        

        tf->tf_cs = USER_CS;
        tf->tf_ds = tf->tf_es = tf->tf_ss = USER_DS;
        tf->tf_eflags |= 0x3000;
    }

……


## 键盘中断处理

键盘中断属于IRQ类型的中断，其IRQ号为1。只要加入对这种类型中断的处理就可以了。


