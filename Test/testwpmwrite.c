#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct expected_char {
    int index;
    char ch;
};

int main() {
    int fd = open("/dev/uniprojdev", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct expected_char ec;
    ec.index = 1;
    ec.ch = 'a';

    ssize_t ret = write(fd, &ec, sizeof(ec));
    if (ret < 0) {
        perror("write");
        return 1;
    }

    printf("Wrote %ld bytes\n", ret);

    close(fd);
    return 0;
}
