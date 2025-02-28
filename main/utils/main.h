#include "stdint.h"

struct split_result {
    char* text;
    int len;
};

uint8_t match(const char* org, const char* new, int osize, int nsize);
int split(const unsigned char* str, int len, char delim, struct split_result* res);
void acked();
int send_uart_cmd(uart_port_t port, void* src, size_t size);