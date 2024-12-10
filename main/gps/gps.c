#include "stdio.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "gps.h"
#include "../utils/main.h"

void gps_start() {
    uart_driver_install(UART_NUM_1, 512, 0, 0, NULL, 9);
    uart_param_config(UART_NUM_1, &gps_uart);
    uart_set_pin(UART_NUM_1, 36, 39, -1, -1);
}

void gps_disable() {
    if(uart_is_driver_installed(UART_NUM_1))
        uart_driver_delete(UART_NUM_1);
}

void read_gps(TimerHandle_t timer) {
    char data[257];
    uart_flush(UART_NUM_1);
    int len = uart_read_bytes(UART_NUM_1, data, 256, 20 / portTICK_PERIOD_MS);
    data[len] = '\0';
    printf("%s", data);

    //parse_gps_data(data, len);
}

void parse_gps_data(char* data, int len) {
    struct split_result res[10];

    int len = split((const unsigned char*)data, len, '\n', res);

    for(int i = 0; i < len; i++) {
        //struct split_result nmea_mes[14];
        //int nmea_len = split(res[i].text, res[i].len, ',', nmea_mes);
        
        struct gps_info gp_info = {
            .utc = 0,
        };
        
        sscanf("$GPGGA,%lf,%lf,%c,%lf,%c,", &(gp_info.utc), &(gp_info.lat), &(gp_info.lat_dir), &(gp_info.logt), &(gp_info.logt_dir));
        
        if(gp_info.utc) {
            if(gps_callbacks.new_gps) {
                gps_callbacks.new_gps(res[i].text, res[i].len, gp_info);
            }
            break;
        }
    };
};