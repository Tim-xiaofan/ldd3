#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>
#include <sys/mman.h>

static const char *path = "/dev/jitimer0";
#define BUF_SIZE 4096

int main(int argc, char *argv[])
{
    int fd = -1;
    char *buf = NULL;

    close(0);
    
    if((fd = open(path, O_RDWR) == -1)) {
        perror("open");
        return -1;
    }
    printf("fd = %d: \n", fd);

    buf = malloc(BUF_SIZE);
    memset(buf, 0, BUF_SIZE);
    if(read(fd, buf, BUF_SIZE) > 0)
    {
        printf("read msg: %s", buf);
    }
    else
    {
        perror("read");
    }

    close(fd);
    return 0;
}
