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

const char *path = "/dev/major_minor0";

int main(int argc, char *argv[])
{
	int fd;
	char buf[128];

	close(0);
	if((fd = open(path, O_RDWR) == -1))
	{
		perror("open");
		return -1;
	}

	printf("fd = %d\n", fd);

	write(fd, "01234", 5);

	memset(buf, 0, sizeof(buf));
	if(read(fd, buf, sizeof(buf)) > 0)
	{
		printf("msg:%s\n", buf);
	}
	else
	{
		perror("read");
	}

	close(fd);
	return 0;
}
