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

static const char *path = "/dev/mmap_test0";
#define MMAP_BUF_SIZE 4096 * 4

int main(int argc, char *argv[])
{
    int fd;
    char buf[128];
    char *mmap_buf = NULL;
    const char *mmap_msg = "It's mmap message.";

    close(0);
    
    if((fd = open(path, O_RDWR) == -1)) {
        perror("open");
        return -1;
    }
    printf("fd = %d: \n", fd);


    mmap_buf = mmap(NULL, MMAP_BUF_SIZE, PROT_READ | PROT_WRITE,
               MAP_SHARED, fd, 0);
    if(mmap_buf == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    snprintf(mmap_buf, strlen(mmap_msg) + 1, "%s", mmap_msg);
    printf("1 mmap_buf: %s\n", mmap_buf);

    if(write(fd, mmap_msg, strlen(mmap_msg))) {}
    printf("2 mmap_buf: %s\n", mmap_buf);

    memset(buf, 0, sizeof(buf));
    if(read(fd, buf, sizeof(buf)) > 0)
    {
        printf("read msg:%s\n", buf);
    }
    else
    {
        perror("read");
    }

    munmap(mmap_buf, MMAP_BUF_SIZE);

    close(fd);
    return 0;
}
