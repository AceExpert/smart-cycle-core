#include "stdint.h"
#include "string.h"

#include "driver/uart.h"

#include "freertos/timers.h"

#include "main.h"

TimerHandle_t uart_timer = NULL;

struct safe_uart_queue
{
    void* data;
    uart_port_t port;
    size_t size;
    struct safe_uart_queue* next;
}* uart_q = NULL;

void add_uart_queue(uart_port_t port, void* src, size_t size) {
    struct safe_uart_queue* upd_ptr = uart_q;
    if (upd_ptr == NULL) {
        uart_q = malloc(sizeof(struct safe_uart_queue));
        upd_ptr = uart_q;
    } else {
        while (upd_ptr->next) {
            upd_ptr = upd_ptr->next;
        }
    };
    char* d = malloc(size);
    strcpy(d, src);
    upd_ptr->data = d;
    upd_ptr->port = port;
    upd_ptr->size = size;
    upd_ptr->next = NULL;
}

void pop_uart_queue() {
    if(uart_q == NULL) return;
    struct safe_uart_queue* temp = uart_q->next;
    free(uart_q->data);
    free(uart_q);
    uart_q = temp;
}

uint8_t match(const char* org, const char* new, int osize, int nsize) {
    if(osize != nsize) return 0;
    for(int i = 0; i < osize; i++) 
      if (org[i] != new[i]) return 0;
    return 1;
}

int split(const unsigned char* str, int len, char delim, struct split_result* res) {
    int wnum = 0, tnum = 0;
    char* txt = malloc(0);

    for(int i = 0; i < len; i++) {
        if(str[i] == delim) {
            res[wnum].text = txt;
            res[wnum++].len = tnum;
            tnum = 0;
            txt = malloc(0);
        } else {
            txt = realloc(txt, tnum+1);
            txt[tnum++] = str[i];
        }
    }
    res[wnum].text = txt;
    res[wnum++].len = tnum;

    return wnum;
}

void uart_check(TimerHandle_t timer) {
    uart_write_bytes(uart_q->port, uart_q->data, uart_q->size);
}

void progress_uart_queue() {
    if(uart_q == NULL) return;
    uart_write_bytes(uart_q->port, uart_q->data, uart_q->size);
    if (uart_timer == NULL) {
        uart_timer = xTimerCreate("uart_timer", pdMS_TO_TICKS(20), pdTRUE, NULL, uart_check);
    } 
    xTimerReset(uart_timer, portMAX_DELAY);
}

void send_uart_cmd(uart_port_t port, void* src, size_t size) {
    add_uart_queue(port, src, size);
    if (uart_q && uart_q->next == NULL) {
        progress_uart_queue();
    }
}

void acked() {
    xTimerStop(uart_timer, portMAX_DELAY);
    pop_uart_queue();
    progress_uart_queue();
}