#include <stdio.h>

#include "driver/uart.h"

static const uart_config_t gps_uart = {
    .baud_rate = 9600,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT
};

struct gps_info {
    double utc;
    double lat;
    double logt;
    char lat_dir;
    char logt_dir;
};

void gps_start();
void gps_disable();
void gps_start_reading();
void gps_stop_reading();
void read_gps(TimerHandle_t timer);
void set_on_gps(void (*on_gps)(const char*, struct gps_info));