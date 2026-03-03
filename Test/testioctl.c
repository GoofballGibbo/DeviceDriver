//Way to test ioctl function
/*it will open the device, 
set a character using ioctl, (userspace passes pointer a char, kernel recieves pointer in arg, driver calls copy_from_user and sets stored_char to new value)
get the character back using ioctl, (driver copies stored_char, copy_to_user changes char in userspace)
read from the device, reads stored_char into buffer)
and print the results
*/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

#define IOCTL_MAGIC 'p'
#define SET_CHAR _IOW(IOCTL_MAGIC, 0, char)
#define GET_CHAR _IOR(IOCTL_MAGIC, 1, char)

int main() {
    int fd;
    char ch;
    char buffer[20];

    fd = open("/dev/uniprojdev", O_RDWR);

    if (fd < 0) {
        perror("Failed to open the device");
        return EXIT_FAILURE;
    }

    printf("Device opened successfully\n");

    ch = 'Z'; // character to set (change this to test different characters)

    if(ioctl(fd, SET_CHAR, &ch) < 0) {
        perror("Failed to set character");
        close(fd);
        return EXIT_FAILURE;
    }

    printf("Character set to: %c\n", ch);

    ch = 0; 

    if(ioctl(fd, GET_CHAR, &ch) < 0) {
        perror("Failed to get character");
        close(fd);
        return EXIT_FAILURE;
    }

    printf("Character retrieved: %c\n", ch);

    memset(buffer, 0, sizeof(buffer));
    
    if (read(fd, buffer, sizeof(buffer)-1) < 0) {
        perror("Failed to read from the device");
        close(fd);
        return EXIT_FAILURE;
    }

    printf("Data read from device: %s\n", buffer);

    close(fd);
    return EXIT_SUCCESS;
}