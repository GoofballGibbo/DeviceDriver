#include "asm/uaccess.h"
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#define DEBUG // debug prints
#define FIFO_SIZE 256
#define INCORRECT_MAX 128
#define UNDO_SIZE 64
#define RESULT_SIZE 64

typedef struct {
  int index;
  char ch;
} expected_char;

typedef struct {
  int index;
  char expected;
  char typed;
  bool has_error;
} keystroke_result;

typedef struct {
  int start_index;
  int end_index;
  int correct;
} word_entry;

typedef struct {
  int correct_words, missed_words, correct_chars, missed_chars, wpm, raw_wpm,
      elapsed_seconds;
} wpm_stats;

static expected_char undo[UNDO_SIZE];
static keystroke_result result[RESULT_SIZE];
static int result_head = 0, result_tail = 0, result_count = 0;
static word_entry current_word = {0};
static bool in_word = false, shift_held = false;
static word_entry incorrect_stack[INCORRECT_MAX];
static int incorrect_top = 0;
static expected_char fifo[FIFO_SIZE];
static int fifo_head = 0;
static int fifo_tail = 0;
static int fifo_count = 0;

static const char keycode_map[KEY_MAX] = {
    [KEY_A] = 'a',           [KEY_B] = 'b',         [KEY_C] = 'c',
    [KEY_D] = 'd',           [KEY_E] = 'e',         [KEY_F] = 'f',
    [KEY_G] = 'g',           [KEY_H] = 'h',         [KEY_I] = 'i',
    [KEY_J] = 'j',           [KEY_K] = 'k',         [KEY_L] = 'l',
    [KEY_M] = 'm',           [KEY_N] = 'n',         [KEY_O] = 'o',
    [KEY_P] = 'p',           [KEY_Q] = 'q',         [KEY_R] = 'r',
    [KEY_S] = 's',           [KEY_T] = 't',         [KEY_U] = 'u',
    [KEY_V] = 'v',           [KEY_W] = 'w',         [KEY_X] = 'x',
    [KEY_Y] = 'y',           [KEY_Z] = 'z',         [KEY_1] = '1',
    [KEY_2] = '2',           [KEY_3] = '3',         [KEY_4] = '4',
    [KEY_5] = '5',           [KEY_6] = '6',         [KEY_7] = '7',
    [KEY_8] = '8',           [KEY_9] = '9',         [KEY_0] = '0',
    [KEY_SPACE] = ' ',       [KEY_DOT] = '.',       [KEY_COMMA] = ',',
    [KEY_APOSTROPHE] = '\'', [KEY_SEMICOLON] = ';',
};
static const char keycode_map_shift[KEY_MAX] = {
    [KEY_A] = 'A',          [KEY_B] = 'B',         [KEY_C] = 'C',
    [KEY_D] = 'D',          [KEY_E] = 'E',         [KEY_F] = 'F',
    [KEY_G] = 'G',          [KEY_H] = 'H',         [KEY_I] = 'I',
    [KEY_J] = 'J',          [KEY_K] = 'K',         [KEY_L] = 'L',
    [KEY_M] = 'M',          [KEY_N] = 'N',         [KEY_O] = 'O',
    [KEY_P] = 'P',          [KEY_Q] = 'Q',         [KEY_R] = 'R',
    [KEY_S] = 'S',          [KEY_T] = 'T',         [KEY_U] = 'U',
    [KEY_V] = 'V',          [KEY_W] = 'W',         [KEY_X] = 'X',
    [KEY_Y] = 'Y',          [KEY_Z] = 'Z',         [KEY_1] = '!',
    [KEY_2] = '@',          [KEY_3] = '#',         [KEY_4] = '$',
    [KEY_5] = '%',          [KEY_6] = '^',         [KEY_7] = '&',
    [KEY_8] = '*',          [KEY_9] = '(',         [KEY_0] = ')',
    [KEY_SPACE] = ' ',      [KEY_DOT] = '>',       [KEY_COMMA] = '<',
    [KEY_APOSTROPHE] = '"', [KEY_SEMICOLON] = ':',
};

/* AI generated Scancode coversion */

static DEFINE_SPINLOCK(wpm_lock);
static DECLARE_WAIT_QUEUE_HEAD(read_wait_queue);
static DECLARE_WAIT_QUEUE_HEAD(write_wait_queue);

static ssize_t mod_write(struct file *file, const char __user *user_buf,
                         size_t len, loff_t *off) {
  expected_char ec;
  unsigned long flags;

  if (len != sizeof(expected_char)) {
    return -EINVAL;
  }

  if (copy_from_user(&ec, user_buf, sizeof(expected_char))) {
    return -EFAULT;
  }

  if (wait_event_interruptible(write_wait_queue, fifo_count < FIFO_SIZE)) {
    pr_warn("Capacity reached");
    return -ERESTARTSYS;
  }

  spin_lock_irqsave(&wpm_lock, flags);

  fifo[fifo_tail] = ec;
  fifo_tail = (fifo_tail + 1) % FIFO_SIZE;
  fifo_count++;

  spin_unlock_irqrestore(&wpm_lock, flags);
  wake_up_interruptible(&read_wait_queue);

  return sizeof(expected_char);
}

MODULE_DESCRIPTION("placeholder description"); // if you don't include this
                                               // kbuild gets very mad
MODULE_LICENSE("GPL"); // same here (why must you force me to use GPL)

static ssize_t mod_read(struct file *file, char __user *buf, size_t count,
                        loff_t *ppos) {
  struct keystroke_result result;
  unsigned long flags;

  if (count != sizeof(result))
    return -EINVAL;

  /* block until a result is in the queue */
  if (wait_event_interruptible(read_wait_queue, result_count > 0))
    return -ERESTARTSYS;

  spin_lock_irqsave(&wpm_lock, flags);
  result = result_queue[result_head];
  result_head = (result_head + 1) % RESULT_SIZE;
  result_count--;
  spin_unlock_irqrestore(&wpm_lock, flags);

  if (copy_to_user(buf, &result, sizeof(result)))
    return -EFAULT;
  return sizeof(result);
}

static unsigned int mod_poll(struct file *file, poll_table *wait) {
  unsigned int mask = 0;
  unsigned long flags;
  poll_wait(file, &read_wait_queue, wait);
  poll_wait(file, &write_wait_queue, wait);
  spin_lock_irqsave(&wpm_lock, flags);
  if (result_count > 0)
    mask |= POLLIN | POLLRDNORM;
  if (fifo_count < FIFO_SIZE)
    mask |= POLLOUT | POLLWRNORM;
  spin_unlock_irqrestore(&wpm_lock, flags);
  return mask;
}
static int usb_probe(struct usb_interface *intf,
                     const struct usb_device_id *id) {
  printk(KERN_INFO "usb plugged in\n");
  return 0;
}

static bool is_delimiter(char c) {
  return c == ' ' || c == '\t' || c = '\'' || c == '"';
}

static void finalise_word(int index) {
  if (!in_word) {
    return;
  }
  current_word.end_index = index;
  if (current_word.has_error) {
    if (incorrect_top < INCORRECT_MAX) {
      incorrect_stack[incorrect_top++] = current_word;
    }
    missed_word++;
  } else {
    correct_words++;
  }
  in_word = false;
  current_word.has_error = false;
  current_word.start_index = 0;
  current_word.end_index = 0;
}

static void wpm_event(struct input_handle *handle, unsigned int type,
                      unsigned int code, int value) {
  unsigned long flags;

  char ascii;

  if (type != EV_KEY)
    return;
  if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
    shiftheld = {value == 1} return;
  }
  if (value != 1) {
    return;
  }

  if (code == BACKSPACE) {
    spin_lock_irqsave(&wpm_lock, flags);
    if (undo_top > 0) {
      expected_char ec = undo_stack[--undo_top];
      fifo_head = (fifo_head - 1 + FIFO_SIZE) % FIFO_SIZE;
      fifo[fifo_head] = ec;
      fifo_count++;
      correct_char--;
      current_word.has_error = false;
    }
    spin_unlock_irqrestore(&wpm_lock, flags);
    wake_up_interruptible(&write_wait_queue);
    return;
  }

  ascii = shift_held ? keycode_map_shift[code] : keycode_map[code];
  if (ascii == 0)
    return;

  spin_lock_irqsave(&wpm_lock, flags);
  if (fifo_count == 0) {
    spin_unlock_irqrestore(&wpm_lock, flags);
  }

  expected_char expected = fifo[fifo_head];
  if (ascii == expected.ch) {
    fifo_head = (fifo_head + 1) % FIFO_SIZE;
    fifo_count--;
    undo_stack[undo_top % UNDO_SIZE] = expected;
    if (undo_top < UNDO_SIZE) {
      undo_top++;
      correct_chars++;
    }
    if (correct_chars == 1) {
      test_start = ktime.get();
    }
    if (is_delimiter(ascii)) {
      finalise_word(expected.index);
    } else {
      if (!in_word) {
        in_word = true;
        current_word.start_index = expected.index;
        current_word.has_error = false;
      }
    }
    if (ascii == '\0' || fifo_count == 0) {
      finalise_word(expected.index);
    }
    spin_unlock_irqrestore(&wpm_lock, flags);
    wake_up_interruptible(&write_wait_queue);

  } else {
    missed_chars++;
    current_word.has_error = true;
    spin_unlock_irqrestore(&wpm_lock, flags);
  }
}
}

static long wpm_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
  struct wpm_stats stats;
  unsigned long flags;
  ktime_t now;
  s64 em;

  switch (cmd) {
  case WPM_START:
    spin_lock_irqsave(&wpm_lock, flags);
    driver_state = STATE_RUNNING;
    test_start = ktime_get();
    spin_unlock_irqrestore(&wpm_lock, flags);
    return 0;

  case WPM_STOP:
    spin_lock_irqsave(&wpm_lock, flags);
    driver_state = STATE_COMPLETE;
    spin_unlock_irqrestore(&wpm_lock, flags);
    return 0;

  case WPM_RESET:
    spin_lock_irqsave(&wpm_lock, flags);
    fifo_head = fifo_tail = fifo_count = 0;
    undo_top = result_head = result_tail = result_count = 0;
    correct_words = missed_words = correct_chars = missed_chars = 0;
    incorrect_top = 0;
    in_word = false;
    driver_state = STATE_IDLE;
    spin_unlock_irqrestore(&wpm_lock, flags);
    return 0;

  case WPM_GET_STATS:
    spin_lock_irqsave(&wpm_lock, flags);
    now = ktime_get();
    em = ktime_to_ms(ktime_sub(now, test_start));
    stats.correct_words = correct_words;
    stats.missed_words = missed_words;
    stats.correct_chars = correct_chars;
    stats.missed_chars = missed_chars;
    stats.elapsed_seconds = (int)(em / 1000);
    stats.wpm = em > 0 ? (int)((correct_words * 60000LL) / em) : 0;
    stats.raw_wpm =
        em > 0 ? (int)(((correct_words + missed_words) * 60000LL) / em) : 0;
    spin_unlock_irqrestore(&wpm_lock, flags);
    if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
      return -EFAULT;
    return 0;

  case WPM_SET_LED:
    return 0; /* no hardware on this platform */

  default:
    return -ENOTTY;
  }
}

static int wpm_connect(struct input_handler *handler, struct input_dev *dev,
                       const struct input_device_id *id) {
  struct input_handle *handle, int error;
  handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
  if (!handle) {
    return -ENOMEM;
  }
  handle->dev = dev;
  handle->handler = handler;
  handlle->name = "wpm_driver";
  error = input_register_handler(handle);
  if (error) {
    kfree(handle);
    return error;
  }
  error = input_open_device(handle);
  if (error) {
    input_unregister_handle(handle);
    kfree(handle);
    return error;
  }
  pr_info("wpm_driver: connected to %s\n", dev->name);
  return 0;
}

static void wpm_disconnect(struct input_handle *handle) {
  input_close_device(handle);
  input_unregister_handle(handle);
  kfree(handle);
}

static void usb_dc(struct usb_interface *intf) {
  printk(KERN_INFO "disconnecting usb\n");
}

int major;
dev_t dev;
static struct cdev cdev;
static struct class *cl;

static int mod_open(struct inode *inode, struct file *fileptr) {
  pr_info("Major device number: %d | Minor device number: %d\n", imajor(inode),
          iminor(inode));

  pr_info("Fileptr->f_pos : %lld\n", fileptr->f_pos);
  pr_info("Fileptr->f_mode : %lld\n", fileptr->f_mode);
  pr_info("Fileptr->f_flags: %lld\n", fileptr->f_flags);
  return 0;
}

static int mod_release(struct inode *inode, struct file *fileptr) {
  pr_info("The file has closed\n");
  return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = mod_read,
    .open = mod_open,
    .release = mod_release,
    .write = mod_write,
    .poll = mod_poll,
};

struct usb_device_id usbdid[] = {{USB_DEVICE(0x2e8a, 0x0009)}, {}};

static struct usb_driver usbd = {
    .name = "uniproject",
    .probe = usb_probe,
    .disconnect = usb_dc,
    .id_table = usbdid,
};

static int __init custom_init(void) {
  // get a device number (cat /proc/devices)
  if (alloc_chrdev_region(&dev, 0, 1, "uniproject")) {
    printk(KERN_ERR "device allocation failed!\n");
    return -ECANCELED;
  }

  // make a device class (ls /sys/class)
  if (!(cl = class_create("uniprojclass"))) {
    printk(KERN_ERR "class creation failed!\n");
    unregister_chrdev_region(dev, 1);
    return -ECANCELED;
  }

  // initialize chardevice
  cdev_init(&cdev, &fops);
  if (cdev_add(&cdev, dev, 1) < 0) {
    printk(KERN_ERR "chardevice init failed!\n");
    class_destroy(cl);
    unregister_chrdev_region(dev, 1);
    return -ECANCELED;
  }

  // create the device (ls /dev)
  if (!device_create(cl, NULL, dev, NULL, "uniprojdev")) {
    printk(KERN_ERR "device creation failed!\n");
    cdev_del(&cdev);
    class_destroy(cl);
    unregister_chrdev_region(dev, 1);
    return -ECANCELED;
  }

  if (usb_register(&usbd) < 0) {
    printk(KERN_ERR "USB registration failed!\n");
    device_destroy(cl, dev);
    cdev_del(&cdev);
    class_destroy(cl);
    unregister_chrdev_region(dev, 1);
    return -ECANCELED;
  }

  major = MAJOR(dev);

#ifdef DEBUG
  printk(KERN_INFO "placeholder loaded (major num: %d)\n", major);
#endif

  return 0;
}
static void __exit custom_exit(void) {
  usb_deregister(&usbd);
  device_destroy(cl, dev);
  cdev_del(&cdev);
  class_destroy(cl);
  unregister_chrdev_region(dev, 1);

#ifdef DEBUG
  printk(KERN_INFO "placeholder ended\n");
#endif
}

module_init(custom_init);
module_exit(custom_exit);
