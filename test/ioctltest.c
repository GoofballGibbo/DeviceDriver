#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define IOCTL_MAGIC 'p'
#define SET_CHAR _IOW(IOCTL_MAGIC, 0, char)
#define GET_CHAR _IOR(IOCTL_MAGIC, 1, char)

#define DEVICE "/dev/uniprojdev"

int main(void) {
    int fd;
    char c;

    fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    /* --- test GET_CHAR: read the default value ('X') --- */
    if (ioctl(fd, GET_CHAR, &c) < 0) {
        perror("ioctl GET_CHAR");
        close(fd);
        return 1;
    }
    printf("GET_CHAR (initial):  '%c'\n", c);

    /* --- test SET_CHAR: write a new value --- */
    c = 'Z';
    if (ioctl(fd, SET_CHAR, &c) < 0) {
        perror("ioctl SET_CHAR");
        close(fd);
        return 1;
    }
    printf("SET_CHAR:            '%c'\n", c);

    /* --- test GET_CHAR again: confirm the value changed --- */
    if (ioctl(fd, GET_CHAR, &c) < 0) {
        perror("ioctl GET_CHAR");
        close(fd);
        return 1;
    }
    printf("GET_CHAR (after set): '%c'\n", c);

    close(fd);
    return 0;
}
