/*
 * wpm_common.h — shared definitions between kernel module and userspace.
 * Both sides include this file so structs and ioctl numbers stay in sync.
 */
#ifndef WPM_COMMON_H
#define WPM_COMMON_H
#include <sys/ioctl.h>

typedef struct {
    int index;
    char ch;
} expected_char_t;
typedef struct {
    int index;
    char expected;
    char typed;
    int correct;
} keystroke_result_t;
typedef struct {
    int correct_words, missed_words, correct_chars, missed_chars, elapsed_seconds, wpm, raw_wpm;
} wpm_stats_t;

#define WPM_MAGIC 'W'
#define WPM_START _IO(WPM_MAGIC, 0)
#define WPM_STOP _IO(WPM_MAGIC, 1)
#define WPM_RESET _IO(WPM_MAGIC, 2)
#define WPM_GET_STATS _IOR(WPM_MAGIC, 3, wpm_stats_t)
#define WPM_SET_LED _IOW(WPM_MAGIC, 4, int)
#define DEVICE "/dev/uniprojdev"

#endif
