/**
 * 从内核模块读取tcp流并dump到pcap文件 
 */
#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <vector>
#include <errno.h>
#include <cstring>
#include <sys/fcntl.h>
#include <cstdlib>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <string_view>
#include <cassert>
#include <pcap/pcap.h>
#include <cstdio>
#include <algorithm>
#include <sys/time.h>

namespace 
{

bool run = true;
constexpr const char *PATH = "/dev/dump_tcp";
constexpr const char *PCAP_PATH = "./tcp.pcap";
constexpr int MAX_EVENTS = 2;
constexpr int LINKTYPE = DLT_EN10MB; // Ethernet 
constexpr int SNAPLEN = 65535;
constexpr int BUF_SIZE = SNAPLEN;
int wake_fd = -1;

void handle_signal(int signum)
{
    if(signum == SIGINT || signum == SIGKILL) {
        run = false;
        size_t one = 1;
        if(write(wake_fd, &one, sizeof(one))) {
        }
    }
}

void handle_error(const std::string_view& info, bool need_exit = true)
{
    std::cerr << info << ": " << errno << "(" << strerror(errno) << ")\n";
    if(need_exit) {
        exit(EXIT_FAILURE);
    }
}

void set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, NULL);
    if(flags == -1){
        handle_error("fcntl(F_GETFL)");
    }

    if(!(flags & O_NONBLOCK)) {
        if(fcntl(fd, F_SETFL, flags|O_NONBLOCK) == -1) {
            handle_error("fcntl(F_SETFL)");
        }
    }
}

void epoll_register(int efd, int fd)
{
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = (EPOLLIN | EPOLLERR);
    ev.data.fd = fd;
    if(epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev)){
        handle_error("epoll_ctl(ADD)");
    }
}

void epoll_unregister(int efd, int fd)
{
    if(epoll_ctl(efd, EPOLL_CTL_DEL, fd, nullptr)) {
        handle_error("epoll_ctl(DEL)");
    }
}

}

int main(void) 
{
    signal(SIGINT, handle_signal);
    signal(SIGKILL, handle_signal);

    // 创建pcap_ctx
    pcap_t *pcap_ctx = pcap_open_dead(LINKTYPE, SNAPLEN);
    if(!pcap_ctx){
        handle_error("pcap_open_dead");
    }
    
    // 创建pcap_dump_ctx
    pcap_dumper_t* pcap_dump_ctx = pcap_dump_open(pcap_ctx, PCAP_PATH);
    if(!pcap_dump_ctx) {
        handle_error(pcap_geterr(pcap_ctx));
    }
    
    int efd = epoll_create1(EPOLL_CLOEXEC);
    if(efd == -1) {
        handle_error("epoll_create1");
    }
    
    int fd = open(PATH, O_RDWR);
    if(fd == -1) {
        handle_error("open");
    }
    std::cout << "fd = " << fd << std::endl;
    set_nonblock(fd);
    epoll_register(efd, fd);
    
    wake_fd = eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK);
    if(wake_fd == -1) {
        handle_error("eventfd");
    }
    std::cout << "wake_fd = " << wake_fd << std::endl;
    set_nonblock(wake_fd);
    epoll_register(efd, wake_fd);
 
    std::vector<struct epoll_event> events(MAX_EVENTS);
    std::vector<char> buffer(BUF_SIZE);
    size_t count = 0; 

    while(run) {
        int nevent = epoll_wait(efd, events.data(), events.size(), -1);
        if(nevent == -1) {
            handle_error("epoll_wait", false);
            break;
        } else if(nevent == 0) {
            continue;
        }
        for(int i = 0; i < nevent; ++i) {
            const struct epoll_event& ev = events[i];
            if(ev.data.fd == fd) { // 内核模块fd
                int nread = read(fd, buffer.data(), buffer.size());
                if(nread == -1 && errno != EAGAIN) {
                    handle_error("read", false);
                    epoll_unregister(efd, fd);
                    close(fd);
                    continue;
                }

                // create a packet
                struct timeval tv;
                gettimeofday(&tv, nullptr);
                struct pcap_pkthdr pkt = {
                    .ts = tv,
                    .caplen = (unsigned)std::min(nread, SNAPLEN),
                    .len = (unsigned)nread
                };
                // dump the packet
                pcap_dump(reinterpret_cast<u_char *>(pcap_dump_ctx), 
                            &pkt, 
                            reinterpret_cast<u_char *>(buffer.data()));
                printf("count=%lu\r", ++count);
                fflush(stdout);
            } else { // eventfd
                std::cout << "wake_fd = " << wake_fd 
                    << ", data.fd = " << ev.data.fd << std::endl;
                assert(wake_fd == ev.data.fd);
                size_t count;
                if(read(wake_fd, &count, sizeof(count)) != sizeof(count)) {
                    epoll_unregister(efd, wake_fd);
                }
            }
        }
    }
    printf("count=%lu\n", count);

    close(wake_fd);
    close(fd);
    close(efd);
    pcap_dump_close(pcap_dump_ctx);
    pcap_close(pcap_ctx);
    std::cout << "All done.\n";
    return 0;
}
