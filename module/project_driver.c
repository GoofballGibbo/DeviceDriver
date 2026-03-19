
/* project_driver.c
 * Linux kernel module implementing a WPM (words-per-minute) typing test driver.
 *
 * Overview:
 *   - Registers as a character device (/dev/uniprojdev) so a userspace app can
 *     feed in the expected text (write) and read back per-keystroke results (read).
 *   - Hooks into the Linux input subsystem to intercept raw keyboard events
 *   - Communicates with an microcontroller over USB serial to drive an
 *     RGB LED: green on correct keypress, red on mistake, off on backspace.
 *   - Exposes live stats via /proc (wpm_state, wpm_stats, wpm_errors).
 */

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/wait.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("a group of CS students");
MODULE_DESCRIPTION("WPM Typing Tester Driver");
MODULE_VERSION("0.1.0");

/* #define DEBUG // debug prints */

#define DEVFILE "/dev/ttyACM0" // terrible solution. if i can do better i will

#define DEVICE_NAME "uniprojdev"
#define CLASS_NAME "uniprojclass"

/* Circular buffer sizes.
 * FIFO_SIZE: how many expected characters the userapp can queue ahead of time.
 * UNDO_SIZE: how many keystrokes can be undone with backspace.
 * RESULT_SIZE: how many keystroke results can be buffered before the oldest is dropped. */

#define FIFO_SIZE 256
#define UNDO_SIZE 256
#define RESULT_SIZE 64
#define INCORRECT_MAX 128


/* ioctl command definitions.
 * WPM_MAGIC is the unique type byte that distinguishes our commands.
 * _IO, _IOR, _IOW are kernel macros that encode direction + size into the cmd number. */

#define WPM_MAGIC 'W'
#define WPM_START _IO(WPM_MAGIC, 0)
#define WPM_STOP _IO(WPM_MAGIC, 1)
#define WPM_RESET _IO(WPM_MAGIC, 2)
#define WPM_GET_STATS _IOR(WPM_MAGIC, 3, wpm_stats)
#define WPM_SET_LED _IOW(WPM_MAGIC, 4, int)

typedef struct {
    int index;
    char ch;
} expected_char;

typedef struct {
    int index;
    char expected;
    char typed;
    int correct;
} keystroke_result;

typedef struct {
    int start_index;
    int end_index;
    bool has_error;
} word_entry;

typedef struct {
    int correct_words, missed_words, correct_chars, missed_chars, wpm, raw_wpm, elapsed_seconds;
} wpm_stats;

typedef enum { STATE_IDLE, STATE_RUNNING, STATE_COMPLETE } driver_state_t;

static driver_state_t driver_state = STATE_IDLE;
static ktime_t test_start;
static int correct_words = 0, missed_words = 0;
static int correct_chars = 0, missed_chars = 0;

static expected_char undo_stack[UNDO_SIZE];
static int undo_top = 0;

static keystroke_result result_queue[RESULT_SIZE];
static int result_head = 0, result_tail = 0, result_count = 0;

static expected_char fifo[FIFO_SIZE];
static int fifo_head = 0, fifo_tail = 0, fifo_count = 0;

static word_entry current_word = {0};
static bool in_word = false;
static bool shift_held = false;

static word_entry incorrect_stack[INCORRECT_MAX];
static int incorrect_top = 0;

static DEFINE_SPINLOCK(wpm_lock);
static DECLARE_WAIT_QUEUE_HEAD(read_wait_queue);
static DECLARE_WAIT_QUEUE_HEAD(write_wait_queue);

int major;               // our device's major number
dev_t dev;               // similar to above i think..? idk
static struct cdev cdev; // character device
static struct class* cl; // device class (needed to make device)

static struct usb_device* usb_dev = NULL; // usb_device struct. if we have this, the device is present

static struct file* usb_fp = NULL; // file pointer for the terrible solution

static const char keycode_map[KEY_MAX] = {
    [KEY_A] = 'a',     [KEY_B] = 'b',   [KEY_C] = 'c',     [KEY_D] = 'd',           [KEY_E] = 'e',         [KEY_F] = 'f',     [KEY_G] = 'g',       [KEY_H] = 'h', [KEY_I] = 'i',
    [KEY_J] = 'j',     [KEY_K] = 'k',   [KEY_L] = 'l',     [KEY_M] = 'm',           [KEY_N] = 'n',         [KEY_O] = 'o',     [KEY_P] = 'p',       [KEY_Q] = 'q', [KEY_R] = 'r',
    [KEY_S] = 's',     [KEY_T] = 't',   [KEY_U] = 'u',     [KEY_V] = 'v',           [KEY_W] = 'w',         [KEY_X] = 'x',     [KEY_Y] = 'y',       [KEY_Z] = 'z', [KEY_1] = '1',
    [KEY_2] = '2',     [KEY_3] = '3',   [KEY_4] = '4',     [KEY_5] = '5',           [KEY_6] = '6',         [KEY_7] = '7',     [KEY_8] = '8',       [KEY_9] = '9', [KEY_0] = '0',
    [KEY_SPACE] = ' ', [KEY_DOT] = '.', [KEY_COMMA] = ',', [KEY_APOSTROPHE] = '\'', [KEY_SEMICOLON] = ';', [KEY_SLASH] = '/', [KEY_KPSLASH] = '/',

};
static const char keycode_map_shift[KEY_MAX] = {
    [KEY_A] = 'A',     [KEY_B] = 'B',   [KEY_C] = 'C',     [KEY_D] = 'D',          [KEY_E] = 'E',         [KEY_F] = 'F',     [KEY_G] = 'G',       [KEY_H] = 'H', [KEY_I] = 'I',
    [KEY_J] = 'J',     [KEY_K] = 'K',   [KEY_L] = 'L',     [KEY_M] = 'M',          [KEY_N] = 'N',         [KEY_O] = 'O',     [KEY_P] = 'P',       [KEY_Q] = 'Q', [KEY_R] = 'R',
    [KEY_S] = 'S',     [KEY_T] = 'T',   [KEY_U] = 'U',     [KEY_V] = 'V',          [KEY_W] = 'W',         [KEY_X] = 'X',     [KEY_Y] = 'Y',       [KEY_Z] = 'Z', [KEY_1] = '!',
    [KEY_2] = '@',     [KEY_3] = '#',   [KEY_4] = '$',     [KEY_5] = '%',          [KEY_6] = '^',         [KEY_7] = '&',     [KEY_8] = '*',       [KEY_9] = '(', [KEY_0] = ')',
    [KEY_SPACE] = ' ', [KEY_DOT] = '>', [KEY_COMMA] = '<', [KEY_APOSTROPHE] = '"', [KEY_SEMICOLON] = ':', [KEY_SLASH] = '?', [KEY_KPSLASH] = '/',
};


// write to the actual device. should only ever receive 3 bytes but i'm being safe
static int write_led(char rgb[3], size_t n) {
    if (!usb_dev) {
        printk(KERN_ERR "no device!");
        return -ENODEV;
    }

    if (IS_ERR(usb_fp)) {
        printk(KERN_ERR "could not open rp2350!");
        return -ENOENT;
    }

    loff_t off = 0;
    kernel_write(usb_fp, rgb, n, &off);

    return 0;
}

/* mod_read - read one keystroke_result out of the result queue into userspace.
 * Blocks until at least one result is available (wait_event_interruptible).
 * Returns sizeof(keystroke_result) on success, or a negative error code. */

static ssize_t mod_read(struct file* fd, char __user* buf, size_t nbytes, loff_t* offset) {
#ifdef DEBUG
    printk(KERN_INFO "reading %zu bytes\n", nbytes);
#endif

    keystroke_result res;
    unsigned long flags;

    if (nbytes != sizeof(res))
        return -EINVAL;

    if (wait_event_interruptible(read_wait_queue, result_count > 0))
        return -ERESTARTSYS;

    spin_lock_irqsave(&wpm_lock, flags);
    res = result_queue[result_head];
    result_head = (result_head + 1) % RESULT_SIZE;
    result_count--;
    spin_unlock_irqrestore(&wpm_lock, flags);

    if (copy_to_user(buf, &res, sizeof(res)))
        return -EFAULT;
    return sizeof(res);
}


/* mod_write - accept one expected_char from userspace into the FIFO.
 * The userapp calls this repeatedly to pre-load the full test string before
 * (or during) the test.  Blocks if the FIFO is full. */

static ssize_t mod_write(struct file* fd, const char __user* buf, size_t nbytes, loff_t* offset) {
    expected_char ec;
    unsigned long flags;

    if (nbytes != sizeof(expected_char))
        return -EINVAL;
    if (copy_from_user(&ec, buf, sizeof(expected_char)))
        return -EFAULT;

    if (wait_event_interruptible(write_wait_queue, fifo_count < FIFO_SIZE))
        return -ERESTARTSYS;

    spin_lock_irqsave(&wpm_lock, flags);
    fifo[fifo_tail] = ec;
    fifo_tail = (fifo_tail + 1) % FIFO_SIZE;
    fifo_count++;
    spin_unlock_irqrestore(&wpm_lock, flags);

    wake_up_interruptible(&read_wait_queue);
    return sizeof(expected_char);
}

static int mod_open(struct inode* inode, struct file* fileptr) {
#ifdef DEBUG
    printk(KERN_INFO "uniprojdev: opened (major=%d minor=%d)\n", imajor(inode), iminor(inode));
#endif
    return 0;
}

static int mod_release(struct inode* inode, struct file* fileptr) {
#ifdef DEBUG
    printk(KERN_INFO "uniprojdev: closed\n");
#endif
    return 0;
}

/* mod_poll - allow userspace to use select()/poll() on our device.
 * Returns POLLIN if there are results to read, POLLOUT if there is FIFO space. */

static unsigned int mod_poll(struct file* file, poll_table* wait) {
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


static bool is_delimiter(char c) { return c == ' ' || c == '\t' || c == '\'' || c == '"'; }

static void finalise_word(int index) {
    if (!in_word)
        return;
    current_word.end_index = index;
    if (current_word.has_error) {
        if (incorrect_top < INCORRECT_MAX)
            incorrect_stack[incorrect_top++] = current_word;
        missed_words++;
    } else {
        correct_words++;
    }
    in_word = false;
    current_word = (word_entry){0};
}

/* post_result - record a keystroke outcome and signal the LED.
 * Called from wpm_event() after the spinlock is released (write_led may sleep).
 *
 * If the result queue is full, the oldest entry is silently dropped to make room —
 * we prefer losing old data over blocking in an input event handler.
 *
 * LED colours: green = correct, red = mistake, off = backspace. */

static void post_result(int index, char expected, char typed, bool correct) {
    unsigned long flags;
    spin_lock_irqsave(&wpm_lock, flags);
    if (result_count == RESULT_SIZE) {

        result_head = (result_head + 1) % RESULT_SIZE;
        result_count--;
    }
    result_queue[result_tail] = (keystroke_result){
        .index = index,
        .expected = expected,
        .typed = typed,
        .correct = correct ? 1 : 0,
    };
    result_tail = (result_tail + 1) % RESULT_SIZE;
    result_count++;
    spin_unlock_irqrestore(&wpm_lock, flags);
    wake_up_interruptible(&read_wait_queue);
    if (typed == '\b') {
        char off[3] = {0, 0, 0};
        write_led(off, 3);
    } else if (correct) {
        char green[3] = {0, 0x10, 0};
        write_led(green, 3);
    } else {
        char red[3] = {0x10, 0, 0};
        write_led(red, 3);
    }
}

/* wpm_event - input subsystem callback, fired for every key event on connected keyboards.
 *
 * This runs in interrupt context so it must not sleep.  All shared state is
 * protected by wpm_lock.  post_result() is called *after* unlocking so that
 * kernel_write() (which can sleep) is never called under the spinlock.
 *
 * Flow:
 *   1. Ignore anything that isn't a key-down event (EV_KEY, value==1).
 *   2. Track Shift state separately.
 *   3. On BACKSPACE: pop the undo stack and push the char back onto the FIFO.
 *   4. On any other key: translate the scancode to ASCII, compare against the
 *      head of the FIFO, and record correct/incorrect accordingly. */

static void wpm_event(struct input_handle* handle, unsigned int type, unsigned int code, int value) {
    unsigned long flags;
    char ascii;

    if (type != EV_KEY)
        return;

    shift_held = test_bit(KEY_LEFTSHIFT, handle->dev->key) || test_bit(KEY_RIGHTSHIFT, handle->dev->key);

    if (value != 1)
        return;

    spin_lock_irqsave(&wpm_lock, flags);

    if (driver_state != STATE_RUNNING) {
        spin_unlock_irqrestore(&wpm_lock, flags);
        return;
    }

    if (code == KEY_BACKSPACE) {
        if (undo_top > 0) {
            undo_top--;
            expected_char ec = undo_stack[undo_top % UNDO_SIZE];
            fifo_head = (fifo_head - 1 + FIFO_SIZE) % FIFO_SIZE;
            fifo[fifo_head] = ec;
            fifo_count++;

            if (correct_chars > 0)
                correct_chars--;
            spin_unlock_irqrestore(&wpm_lock, flags);

            post_result(ec.index, ec.ch, '\b', false);
        } else {
            spin_unlock_irqrestore(&wpm_lock, flags);
        }
        return;
    }

    ascii = shift_held ? keycode_map_shift[code] : keycode_map[code];
    if (ascii == 0) {
        spin_unlock_irqrestore(&wpm_lock, flags);
        return;
    }

    if (fifo_count == 0) {
        spin_unlock_irqrestore(&wpm_lock, flags);
        return;
    }

    expected_char expected = fifo[fifo_head];

    if (ascii == expected.ch) {

        fifo_head = (fifo_head + 1) % FIFO_SIZE;
        fifo_count--;
        undo_stack[undo_top % UNDO_SIZE] = expected;
        if (undo_top < UNDO_SIZE) {
            undo_top++;
        }


        correct_chars++;

        if (correct_chars == 1)
            test_start = ktime_get();

        if (is_delimiter(ascii)) {
            finalise_word(expected.index);
        } else if (!in_word) {
            in_word = true;
            current_word.start_index = expected.index;
            current_word.has_error = false;
        }

        if (fifo_count == 0) {
            finalise_word(expected.index);
            driver_state = STATE_COMPLETE;
        }

        spin_unlock_irqrestore(&wpm_lock, flags);

        post_result(expected.index, expected.ch, ascii, true);
        wake_up_interruptible(&write_wait_queue);

    } else {

        missed_chars++;
        current_word.has_error = true;
        spin_unlock_irqrestore(&wpm_lock, flags);

        post_result(expected.index, expected.ch, ascii, false);
    }
}

static int wpm_connect(struct input_handler* handler, struct input_dev* dev, const struct input_device_id* id) {
    struct input_handle* handle;
    int error;

    if (dev->id.bustype == BUS_VIRTUAL)
        return -ENODEV;
    if (!test_bit(KEY_A, dev->keybit) || !test_bit(KEY_SPACE, dev->keybit))
        return -ENODEV;

    handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
    if (!handle)
        return -ENOMEM;

    handle->dev = dev;
    handle->handler = handler;
    handle->name = "uniprojdev";

    error = input_register_handle(handle);
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

#ifdef DEBUG
    printk(KERN_INFO "uniprojdev: connected to %s\n", dev->name ? dev->name : "?");
#endif
    return 0;
}

static void wpm_disconnect(struct input_handle* handle) {
    input_close_device(handle);
    input_unregister_handle(handle);
    kfree(handle);
}

static const struct input_device_id wpm_ids[] = {{.flags = INPUT_DEVICE_ID_MATCH_EVBIT, .evbit = {BIT_MASK(EV_KEY)}}, {}};

static struct input_handler wpm_input_handler = {
    .event = wpm_event,
    .connect = wpm_connect,
    .disconnect = wpm_disconnect,
    .name = "uniprojdev",
    .id_table = wpm_ids,
};

/* wpm_ioctl - handle control commands from userspace.
 *
 * WPM_START:     transition to RUNNING state and latch the start time.
 * WPM_STOP:      force transition to COMPLETE (e.g. user gave up).
 * WPM_RESET:     clear all buffers and counters, return to IDLE.
 * WPM_GET_STATS: compute and copy a wpm_stats snapshot to userspace.
 * WPM_SET_LED:   stub — LED is currently driven automatically by post_result(). */

static long wpm_ioctl(struct file* file, unsigned int cmd, unsigned long arg) {
    wpm_stats stats;
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
        stats.raw_wpm = em > 0 ? (int)(((correct_words + missed_words) * 60000LL) / em) : 0;
        spin_unlock_irqrestore(&wpm_lock, flags);
        if (copy_to_user((void __user*)arg, &stats, sizeof(stats)))
            return -EFAULT;
        return 0;

    case WPM_SET_LED:
        char rgb[3] = {0, 0, (char)(arg & 0xFF)};
        return write_led(rgb, 3);

    default:
        return -ENOTTY;
    }
}

/* ── /proc interface ────────────────────────────────────────────────────────
 * Three read-only proc files let you inspect driver state without opening
 * the character device:
 *   /proc/wpm_state  — IDLE / RUNNING / COMPLETE
 *   /proc/wpm_stats  — full stats dump
 *   /proc/wpm_errors — list of word spans that contained errors (start:end) */

static int proc_state_show(struct seq_file* m, void* v) {
    unsigned long flags;
    spin_lock_irqsave(&wpm_lock, flags);
    driver_state_t s = driver_state;
    spin_unlock_irqrestore(&wpm_lock, flags);
    seq_printf(m, "%s\n", s == STATE_IDLE ? "IDLE" : s == STATE_RUNNING ? "RUNNING" : "COMPLETE");
    return 0;
}

static int proc_state_open(struct inode* inode, struct file* file) { return single_open(file, proc_state_show, NULL); }

static int proc_stats_show(struct seq_file* m, void* v) {
    unsigned long flags;
    int cw, mw, cc, mc;
    s64 em;
    driver_state_t s;

    spin_lock_irqsave(&wpm_lock, flags);
    cw = correct_words;
    mw = missed_words;
    cc = correct_chars;
    mc = missed_chars;
    em = ktime_to_ms(ktime_sub(ktime_get(), test_start));
    s = driver_state;
    spin_unlock_irqrestore(&wpm_lock, flags);

    int tw = cw + mw, tc = cc + mc;
    int wpm = em > 0 ? (int)((cw * 60000LL) / em) : 0;
    int raw_wpm = em > 0 ? (int)(((cw + mw) * 60000LL) / em) : 0;

    seq_printf(m, "state:           %s\n", s == STATE_IDLE ? "IDLE" : s == STATE_RUNNING ? "RUNNING" : "COMPLETE");
    seq_printf(m, "wpm:             %d\n", wpm);
    seq_printf(m, "raw_wpm:         %d\n", raw_wpm);
    seq_printf(m, "word_accuracy:   %d%%\n", tw > 0 ? (cw * 100) / tw : 0);
    seq_printf(m, "char_accuracy:   %d%%\n", tc > 0 ? (cc * 100) / tc : 0);
    seq_printf(m, "correct_words:   %d\n", cw);
    seq_printf(m, "missed_words:    %d\n", mw);
    seq_printf(m, "correct_chars:   %d\n", cc);
    seq_printf(m, "missed_chars:    %d\n", mc);
    seq_printf(m, "elapsed_seconds: %lld\n", em / 1000);
    return 0;
}

static int proc_stats_open(struct inode* inode, struct file* file) { return single_open(file, proc_stats_show, NULL); }

static int proc_errors_show(struct seq_file* m, void* v) {
    unsigned long flags;
    int i, top;
    word_entry local[INCORRECT_MAX];

    spin_lock_irqsave(&wpm_lock, flags);
    top = incorrect_top;
    memcpy(local, incorrect_stack, top * sizeof(word_entry));
    spin_unlock_irqrestore(&wpm_lock, flags);

    for (i = 0; i < top; i++)
        seq_printf(m, "%d:%d\n", local[i].start_index, local[i].end_index);
    return 0;
}

static int proc_errors_open(struct inode* inode, struct file* file) { return single_open(file, proc_errors_show, NULL); }

static const struct proc_ops proc_state_fops = {
    .proc_open = proc_state_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static const struct proc_ops proc_stats_fops = {
    .proc_open = proc_stats_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static const struct proc_ops proc_errors_fops = {
    .proc_open = proc_errors_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};


static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = mod_read,
    .write = mod_write,
    .open = mod_open,
    .release = mod_release,
    .poll = mod_poll,
    .unlocked_ioctl = wpm_ioctl,
};


// this gets called whenever a USB device matches the below table usbdid. it's 99% the one we want so we just take it
static int usb_probe(struct usb_interface* intf, const struct usb_device_id* id) {
#ifdef DEBUG
    printk(KERN_INFO "usb plugged in\n");
#endif

    if (usb_dev) {
        return -ENOMEM; // unless we already have one. then we tell the kernel no
    }

    usb_dev = interface_to_usbdev(intf);

    usb_fp = filp_open(DEVFILE, O_WRONLY, 0); // and just open the file now so we don't keep reopening it
                                              // (i hate this solution btw but the usb libraries wer not playing nice with me)

    if (!usb_fp || IS_ERR(usb_fp)) {
        printk(KERN_ERR "could not open rp2350!");
        return -ENOENT;
    }

    loff_t off = 0;
    kernel_write(usb_fp, "\n", 1, &off); // since during testing my bytes would be randomnly offset, the board waits for 1 newline first to start the count

    return 0;
}

// called on disconnect. simple enough
static void usb_dc(struct usb_interface* intf) {
#ifdef DEBUG
    printk(KERN_INFO "disconnecting usb\n");
#endif

    filp_close(usb_fp, NULL);

    usb_dev = NULL;
    usb_fp = NULL;
}

struct usb_device_id usbdid[] = {{USB_DEVICE(0x2e8a, 0x0009)}, {}}; // tells the kernel which USB devices we're making a driver for

static struct usb_driver usbd = {
    .name = "uniproject",
    .probe = usb_probe,
    .disconnect = usb_dc,
    .id_table = usbdid,
};

/* custom_init - module entry point.
 *
 * Initialisation order matters:
 *   1. Allocate a dynamic major number.
 *   2. Create a device class (shows up in /sys/class).
 *   3. Initialise and add the cdev.
 *   4. Create the /dev node via device_create().
 *   5. Register /proc entries.
 *   6. Register the input handler (start intercepting keyboard events).
 *   7. Register the USB driver (start watching for the RP2350).
 *
 * If any step fails, all previously registered resources are cleaned up
 * in reverse order to avoid resource leaks. */

static int __init custom_init(void) {
    int ret;

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

    proc_create("wpm_state", 0444, NULL, &proc_state_fops);
    proc_create("wpm_stats", 0444, NULL, &proc_stats_fops);
    proc_create("wpm_errors", 0444, NULL, &proc_errors_fops);

    ret = input_register_handler(&wpm_input_handler);
    if (ret) {
        printk(KERN_ERR "input_register_handler failed!: %d\n", ret);
        remove_proc_entry("wpm_state", NULL);
        remove_proc_entry("wpm_stats", NULL);
        remove_proc_entry("wpm_errors", NULL);
        device_destroy(cl, dev);
        cdev_del(&cdev);
        class_destroy(cl);
        unregister_chrdev_region(dev, 1);
        return ret;
    }

    if (usb_register(&usbd) < 0) {
        printk(KERN_ERR "USB registration failed!\n");
        input_unregister_handler(&wpm_input_handler);
        remove_proc_entry("wpm_state", NULL);
        remove_proc_entry("wpm_stats", NULL);
        remove_proc_entry("wpm_errors", NULL);
        device_destroy(cl, dev);
        cdev_del(&cdev);
        class_destroy(cl);
        unregister_chrdev_region(dev, 1);
        return -ECANCELED;
    }

    major = MAJOR(dev);
#ifdef DEBUG
    printk(KERN_INFO "uniprojdev: loaded (major=%d)\n", major);
#endif
    return 0;
}

static void __exit custom_exit(void) {
    usb_deregister(&usbd);

    input_unregister_handler(&wpm_input_handler);
    remove_proc_entry("wpm_state", NULL);
    remove_proc_entry("wpm_stats", NULL);
    remove_proc_entry("wpm_errors", NULL);
    device_destroy(cl, dev);
    cdev_del(&cdev);
    class_destroy(cl);
    unregister_chrdev_region(dev, 1);

    if (usb_fp && !IS_ERR(usb_fp)) { // does NULL count as IS_ERR? no clue
        filp_close(usb_fp, NULL);
    }

#ifdef DEBUG
    printk(KERN_INFO "uniprojdev: unloaded\n");
#endif
}

module_init(custom_init);
module_exit(custom_exit);
