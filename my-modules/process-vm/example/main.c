#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>

// .bss
static uint8_t bss[32];
static uint8_t bss1[32] = {0};
static void bss_func(void)
{
    static uint8_t bss2[32];
    bss[0]++;
    bss1[0]++;
    bss2[0]++;
}

// .data
static uint8_t data[32] = {1};
static void data_func(void)
{
    static uint8_t data1[32] = {-1};
    data[0]++;
    data1[0]++;
}

static void 
print_usage(const char *prog_name) 
{
    fprintf(stderr, "Usage: %s -f --file <filename>\n", prog_name);
    exit(EXIT_FAILURE);
}

static void mmap_file(int argc, char *argv[])
{
    int opt;
    const char *filename = NULL;

    struct option long_options[] = {
        {"file", required_argument, 0, 'f'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "f:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'f':
                filename = optarg;
                break;
            default:
                print_usage(argv[0]);
        }
    }

    if (!filename) {
        fprintf(stderr, "filename required\n");
        print_usage(argv[0]);
    }

    int fd = open(filename, O_RDONLY); 
    if (fd == -1) {
        fprintf(stderr, "Failed to open file(%s): %s(%d)\n",
                    filename, strerror(errno), errno);
        exit(EXIT_FAILURE);
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        fprintf(stderr, "Failed to get file state, fd=%d: %s(%d)\n",
                    fd, strerror(errno), errno);
        close(fd);
        exit(EXIT_FAILURE);
    }

    void *addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, (off_t)0);
    if (addr == MAP_FAILED) {
        fprintf(stderr, "Failed to map file: %s(%d)\n",
                    strerror(errno), errno);
        close(fd);
        exit(EXIT_FAILURE);
    }

    close(fd);

    getchar();

    if (munmap(addr, st.st_size)) {
        fprintf(stderr, "Failed to munmap file: %s(%d)\n",
                    strerror(errno), errno);
        exit(EXIT_FAILURE);
    }
}


int main(int argc, char *argv[]) 
{
    bss_func();
    data_func();

    const int BUF_SIZE = 1024 * 128 * 2;
    uint8_t *buf = (uint8_t *)malloc(BUF_SIZE);
    memset(buf, 1, BUF_SIZE);

    printf("pid=%d\n", getpid());

    mmap_file(argc, argv);

    free(buf);
    return 0;
}
