#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

int main(void) 
{
    bss_func();
    data_func();

    const int BUF_SIZE = 1024 * 128 * 2;
    uint8_t *buf = (uint8_t *)malloc(BUF_SIZE);
    memset(buf, 1, BUF_SIZE);

    printf("pid=%d\n", getpid());
    getchar();

    free(buf);
    return 0;
}
