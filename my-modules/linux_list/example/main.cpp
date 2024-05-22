#include <iostream>
#include <unistd.h>
#include <future>
#include <thread>
#include <mutex>
#include <signal.h>
#include <vector>
#include <errno.h>
#include <cstring>
#include <random>
#include <chrono>
#include <sys/fcntl.h>
#include <cstdlib>
#include <signal.h>

namespace 
{

bool run = true;
std::mutex iomutex;
constexpr int BUF_SIZE = 1024;
std::random_device seed{};
constexpr const char *PATH = "/dev/linux_list";

using func_t = void (*)(int);

void handle_signal(int signum)
{
    if(signum == SIGINT || signum == SIGKILL) {
        run = false;
    }
}

void reader(int fd) 
{
    std::mt19937 generator(seed());
    std::uniform_int_distribution<int> ms_distributor(0, 10);
    
    std::vector<char> v(BUF_SIZE);
    
    while(run) {
        //sleep for 0~10ms
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_distributor(generator)));

        int ret = read(fd, v.data(), v.size());
        
        std::lock_guard<std::mutex> lock(iomutex);
        std::cout << "thread" << std::this_thread::get_id() << ": ";
        
        if(ret == -1) {
            if(errno != EAGAIN) {
                std::cerr << "read: " << errno << "(" << strerror(errno) << ")\n";
                break;
            }
        }
        std::cout << "read " << ret << " bytes\n";
    }
}

void writer(int fd) 
{
    std::mt19937 generator(seed());
    std::uniform_int_distribution<int> ms_distributor(0, 10);
    std::uniform_int_distribution<int> size_distributor(32, BUF_SIZE);
    
    std::vector<char> v(BUF_SIZE);
    
    while(run) {
        //sleep for 0~10ms
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_distributor(generator)));
        
        int size = size_distributor(generator);
        int ret = write(fd, v.data(), size);
        
        std::lock_guard<std::mutex> lock(iomutex);
        std::cout << "thread" << std::this_thread::get_id() << ": ";
        
        if(ret != size) {
            if(errno != EAGAIN) {
                std::cerr << "write: " << errno << "(" << strerror(errno) << ")\n";
                break;
            }
        }
        std::cout << "write: " << ret << " bytes\n";
    }
}

}

int main(void) 
{
    signal(SIGINT, handle_signal);
    signal(SIGKILL, handle_signal);

    int fd = open(PATH, O_RDWR);
    if(fd == -1) {
        std::cerr << "open failed: " << errno << "(" << strerror(errno) << ")\n";
        exit(EXIT_FAILURE);
    }

    int cpus = std::thread::hardware_concurrency();
    cpus = (cpus < 2)? 2: cpus;

    {
        std::vector<std::future<void>> futs;
        for(int lcore = 0; lcore < cpus; ++lcore) {
            func_t f = (lcore % 2 == 0) ? reader: writer;
            futs.emplace_back(std::async(f, fd));
        }
    }

    close(fd);
    std::cout << "All done.\n";
    return 0;
}
