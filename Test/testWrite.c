#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/module.h>

#define BUFFER_SIZE 1024

static char device_buffer[BUFFER_SIZE];

static ssize_t drv_write(struct file *filp, const char __user *buf,
                         size_t count, loff_t *f_pos) {

  size_t bytes_to_write;

  if (*f_pos >= BUFFER_SIZE) {
    return 0;
  }

  bytes_to_write = min(count, BUFFER_SIZE - (size_t)*f_pos);

  if (copy_from_user(device_buffer + *f_pos, buf, bytes_to_write)) {
    return -EFAULT;
  }

  *f_pos += bytes_to_write;

  return bytes_to_write;
}
