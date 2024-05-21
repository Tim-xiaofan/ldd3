#include <sys/socket.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define MAX_PAYLOAD  64 /* maximum payload size*/
#define NETLINK_TEST NETLINK_USERSOCK

static struct sockaddr_nl src_addr, dest_addr;
static struct nlmsghdr *nlh = NULL;
static struct iovec iov;
static struct msghdr msg;

#define HANDLE_ERROR(msg) do{\
    fprintf(stderr, "%s: %d(%s)\n", msg, errno, strerror(errno));\
    exit(EXIT_FAILURE);\
} while(0)

int main(void) 
{
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_TEST);
    if(fd == -1) {
        HANDLE_ERROR("socket");
    }

    pid_t pid = getpid();

	memset(&src_addr, 0, sizeof(src_addr));
	src_addr.nl_family = AF_NETLINK;
	src_addr.nl_pid = pid;  /* self pid as portid(类似sin_port，若值为0则内核指定)*/
	src_addr.nl_groups = 0;  /* not in mcast groups */
	if(bind(fd, (struct sockaddr*)&src_addr,
				sizeof(src_addr))) {
        HANDLE_ERROR("bind"); 
    }

	memset(&dest_addr, 0, sizeof(dest_addr));
	dest_addr.nl_family = AF_NETLINK;
	dest_addr.nl_pid = 0;   /* For Linux Kernel */
	dest_addr.nl_groups = 0; /* unicast */

	nlh=(struct nlmsghdr *)malloc(
				NLMSG_SPACE(MAX_PAYLOAD));
	memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
	/* Fill the netlink message header */
	nlh->nlmsg_pid = pid;  /* self pid as portid */
	nlh->nlmsg_flags = 0;
	/* Fill in the netlink message payload */
    int n = snprintf(NLMSG_DATA(nlh), MAX_PAYLOAD, 
                "Hello Kernel, I'm process-%d", pid);
	nlh->nlmsg_len = NLMSG_SPACE(n); // Length of message including header

	printf("pid=%d, payload: %d, nl_msg: %d\n", 
                pid, nlh->nlmsg_len-NLMSG_HDRLEN, nlh->nlmsg_len);

	iov.iov_base = (void *)nlh;
	iov.iov_len = nlh->nlmsg_len;
	msg.msg_name = (void *)&dest_addr;
	msg.msg_namelen = sizeof(dest_addr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if(sendmsg(fd, &msg, 0) != nlh->nlmsg_len) {
		HANDLE_ERROR("sendmsg");
	}

	/* Read message from kernel */
    for(int i = 0; i < 2; ++i) {
        memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
        recvmsg(fd, &msg, 0);
        printf("#%d: Received message payload: %s\n",
                    i, (char*)NLMSG_DATA(nlh));
    }

	/* Close Netlink Socket */
	close(fd);
	return 0;
}
