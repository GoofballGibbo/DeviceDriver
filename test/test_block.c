// test_write_block.c — fills the FIFO until it blocks
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int index;
    char ch;
} expected_char_t;

int main(void) {
    int fd = open("/dev/uniprojdev", O_WRONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    printf("writing 512 chars into FIFO (size 256)...\n");
    printf("should block at char 256 until reader consumes\n\n");

    for (int i = 0; i < 512; i++) {
        expected_char_t ec = {.index = i, .ch = 'a'};

        struct timespec t1, t2;
        clock_gettime(CLOCK_MONOTONIC, &t1);

        write(fd, &ec, sizeof(expected_char_t));

        clock_gettime(CLOCK_MONOTONIC, &t2);

        long ms = (t2.tv_sec - t1.tv_sec) * 1000 + (t2.tv_nsec - t1.tv_nsec) / 1000000;

        // Any write that took >1ms was blocked inside the kernel
        if (ms > 1)
            printf("char [%d] BLOCKED for %ldms\n", i, ms);
        else
            printf("char [%d] written immediately\n", i);
    }

    close(fd);
    return 0;
}
