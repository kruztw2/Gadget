#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stropts.h>

int main()
{
    char input[] = "Hello simple";
    char output[0x100] = {};

    int fd = open("/dev/simple", O_RDWR);
    ioctl(fd, 0x123, NULL);
    write(fd, input, strlen(input));
    read(fd, output, strlen(input));
    printf("output: %s\n", output);
    close(fd);

    return 0;
}