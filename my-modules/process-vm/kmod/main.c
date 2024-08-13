#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/rcupdate.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/kdev_t.h>

MODULE_LICENSE("GPL");
static int pid = -1;
module_param(pid, int, S_IRUGO);

static const size_t BYTES_PER_GB = 1024 * 1024 * 1024;
static const size_t LINE_MAX = 1024 + PATH_MAX;

static struct task_struct *
get_task_by_pid(int pid)
{
    struct task_struct *task;

    rcu_read_lock();
    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (task) {
        get_task_struct(task); // Inrease refcnt
    }
    rcu_read_unlock();

    return task;
}

const char *get_filename(struct file *file, char *buf, size_t size) {
    char *path;

    if (!file || !file->f_path.dentry) {
        return ERR_PTR(-EINVAL);  // Return an error pointer if file or dentry is NULL
    }

    // Get the file path as a string
    path = dentry_path_raw(file->f_path.dentry, buf, size);
    
    if (IS_ERR(path)) {
        return path;  // Return the error pointer if something goes wrong
    }

    return path;
}

/* Indicate if the VMA is a stack for the given task; for
 * /proc/PID/maps that is the stack of the main task.
 */
static int is_stack(struct vm_area_struct *vma)
{
	/*
	 * We make no effort to guess what a given thread considers to be
	 * its "stack".  It's not even well-defined for programs written
	 * languages like Go.
	 */
	return vma->vm_start <= vma->vm_mm->start_stack &&
		vma->vm_end >= vma->vm_mm->start_stack;
}

static int is_heap(struct vm_area_struct *vma) 
{
    return vma->vm_start <= vma->vm_mm->brk &&
        vma->vm_end >= vma->vm_mm->start_brk;
}

// 参考内核函数 show_map_vma: fs\proc\task_mmu.c
static void print_vma(struct vm_area_struct *vma) 
{
    size_t count = 0;
    struct mm_struct *mm = vma->vm_mm;
    struct file *file = vma->vm_file;
    vm_flags_t flags = vma->vm_flags;
    unsigned long long pgoff = 0;
    unsigned long start = vma->vm_start;
    unsigned long end = vma->vm_end;
    const char *name = NULL;
    char *path_buf = NULL;
    char *line_buf = NULL;
    unsigned long ino = 0;
    dev_t dev = 0;
    struct anon_vma_name *anon_name = NULL;

    path_buf = kmalloc(PATH_MAX, GFP_KERNEL);
    line_buf = kmalloc(LINE_MAX, GFP_KERNEL);

    if (file) {
        struct inode *inode = file_inode(file);
        dev = inode->i_sb->s_dev; // 设备号
        ino = inode->i_ino; // inode
        pgoff = ((loff_t)vma->vm_pgoff) << PAGE_SHIFT;
    }

    count += snprintf(&line_buf[count], LINE_MAX-count, "%lx-%lx %c%c%c%c %08llx %02x:%02x %-8lu ", 
                   start, end, 
                   flags & VM_READ ? 'r' : '-',
                   flags & VM_WRITE ? 'w' : '-',
                   flags & VM_EXEC ? 'x' : '-',
                   flags & VM_MAYSHARE ? 's' : 'p',
                   pgoff, MAJOR(dev), MINOR(dev), ino);

    if (mm) {
        anon_name = vma->anon_name;
    }

    if (file) {
        count += snprintf(&line_buf[count], LINE_MAX-count, "%s",
                    get_filename(file, path_buf, PATH_MAX));
        goto done;
    }

    if (vma->vm_ops && vma->vm_ops->name) {
        name = vma->vm_ops->name(vma);
    }

    if (!name) {
        if (!mm) {
            name = "[vdso]";
        } else if (is_heap(vma)) {
            name = "[heap]";
        } else if (is_stack(vma)) {
            name = "[stack]";
        } else if (anon_name) {
            count += snprintf(&line_buf[count], LINE_MAX-count,
                        "[anon:%s]", anon_name->name);
        }
    }

done:
    if (name) {
        count += snprintf(&line_buf[count], LINE_MAX-count, "%s",
                    name);
    }
    printk(KERN_INFO "%s\n", line_buf);
    kfree(path_buf);
    kfree(line_buf);
}


int __init process_vm_init(void)
{
    struct task_struct *task = NULL;
    struct mm_struct *mm = NULL;
    int ret = -EINVAL;
    struct vma_iterator vmi;
    struct vm_area_struct *vma = NULL;

    printk(KERN_INFO "init process_vm\n");

    task = get_task_by_pid(pid);
    if (!task) {
        printk(KERN_ERR "Failed get task by pid: %d\n", pid);
        goto out;
    }

    mm = get_task_mm(task);
    if (!mm) {
        goto mm_failed;
    }

	ret = mmap_read_lock_killable(mm);
	if (ret) {
		goto read_lock_failed;
	}

    printk(KERN_INFO "mm {task_size=%lx}\n", mm->task_size);
    printk(KERN_INFO "mm {stack_vm=%lx}\n", mm->stack_vm);

    // .text
    printk(KERN_INFO "mm .text {start_code=%lx, end_code=%lx, delta=%lx}\n", 
                mm->start_code, mm->end_code, mm->end_code - mm->start_code);
    // .data
    printk(KERN_INFO "mm .data {start_data=%lx, end_data=%lx, delta=%lx}\n", 
                mm->start_data, mm->end_data, mm->end_data - mm->start_data);
    // .bss
    //printk(KERN_INFO "mm .bss  {end_data=%lx, start_brk=%lx, delta=%lx}\n", 
    //            mm->end_data, mm->start_brk, mm->start_brk - mm->end_data);
    
    // .heap
    printk(KERN_INFO "mm .heap {start_brk=%lx, brk=%lx, delta=%lx}\n", 
                mm->start_brk, mm->brk, mm->brk - mm->start_brk);

    printk(KERN_INFO "mm {mmap_base=%lx}\n", mm->mmap_base);
    printk(KERN_INFO "mm {start_stack=%lx}\n", mm->start_stack);

    printk(KERN_INFO "mm max_stack_size=%luGB\n", (mm->start_stack - mm->mmap_base)/BYTES_PER_GB);

    vma_iter_init(&vmi, mm, 0);
    for_each_vma(vmi, vma) { 
        print_vma(vma);
    }

    ret = 0;
    mmap_read_unlock(mm);
read_lock_failed:
    mmput(mm);
mm_failed:
    put_task_struct(task);
out:
    return ret;
}

void __exit process_vm_exit(void)
{
    printk(KERN_INFO "exit process_vm\n");
}


module_init(process_vm_init);
module_exit(process_vm_exit);
