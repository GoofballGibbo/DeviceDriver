#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/minmax.h>

#define BUFFER_SIZE 1024

#define FIFO_SIZE 256

typedef struc{
   int index;
   char ch;
}expected_char;

static expected_char fifo(FIFO_SIZE);

static int fifo_head = 0;
static int fifo_tail = 0;
static int fifo_count = 0;
static DEFINE_SPINLOCK(wpm);
static DECLARE_WAIT_QUEUE_HEAD(read_wait_queue);
static DECLARE_WAIT_QUEUE_HEAD(write_wait_queue);




static char device_buffer[BUFFER_SIZE];

static ssize_t drv_write(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){
        
	size_t bytes_to_write;

	expected_char ec;

	unsigned long flags;

	

	if(count != sizeof(expected_char)){
            return -EINVALUE;
	}
	if(copy_to_user( buf, &ec, sizeof(expected_char))){
		return -EFAULT;
	}

 	if(wait_event_interruptible(wrtie_wait_queue, fifo_count < FIFOSIZE)){
           return -ERESTARTSYS;
	}

	spin_lock_irqsave(&wpm_lock, flags);

	fifo[fifo_tail] = ec;
	fifo_tail = (fifo_tail + 1) % FIFO_SIZE;
	fifo_count ++;

	spin_unlock_irqrestore(&wpm_lock,flags);
	wake_up_interruptible(&read_wait_queue);

	return sizeof(expected_char);
}

