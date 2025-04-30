#include "stdio.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "gps.h"
#include "../utils/main.h"

TimerHandle_t gps_timer = NULL;

struct {
    void (*on_gps)(const char*, struct gps_info, int tlen);
} gps_callbacks = {NULL};

void set_on_gps(void (*on_gps)(const char*, struct gps_info, int tlen)) {
    gps_callbacks.on_gps = on_gps;
}

void gps_start() {
    uart_driver_install(UART_NUM_1, 512, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &gps_uart);
    uart_set_pin(UART_NUM_1, -1, 36, -1, -1);
}

void gps_disable() {
    if(uart_is_driver_installed(UART_NUM_1))
        uart_driver_delete(UART_NUM_1);
}

void gps_start_reading() {
    if(gps_timer == NULL) {
        gps_timer = xTimerCreate("gps_timer", pdMS_TO_TICKS(5000), pdTRUE, NULL, read_gps);
    }
    read_gps(NULL);
    xTimerReset(gps_timer, portMAX_DELAY);
};

void gps_stop_reading() {
    if(gps_timer != NULL) {
        if(xTimerIsTimerActive(gps_timer) != pdFALSE)
            xTimerStop(gps_timer, portMAX_DELAY);
    };
}

void read_gps(TimerHandle_t timer) {
    char data;

    uart_flush(UART_NUM_1);
    int t_len = 0;
    char* tag = malloc(1);
    while (1) {
        int len = uart_read_bytes(UART_NUM_1, &data, 1, 20 / portTICK_PERIOD_MS);
        if(len) {
            if(!t_len && data != '$') continue;
            else if (!t_len) {
                tag[0] = data;
                t_len++;
            } else if (t_len) {
                if(t_len == 6 && !match("$GPGGA", tag, 6, 6)) {
                    t_len = 0;
                    free(tag);
                    tag = malloc(1);
                    continue;
                }
                tag = realloc(tag, t_len+1);
                if(t_len >= 6) {
                    if(data == '\n') {
                        tag[t_len++] = '\n';
                        break;
                    };
                };
                tag[t_len++] = data;
            }
        } else {
            t_len = 0;
            free(tag);
            tag = malloc(1);
            continue;
        }
    }

    struct gps_info gp_info = {
        .utc = 0,
    };

    /*free(tag);
    tag = "$GPGGA,1783.0,2219.30970,N,08717.92447,E,0,00,99.99,,,,,,*48\n";
    t_len = 61;*/

    sscanf(tag, "$GPGGA,%lf,%lf,%c,%lf,%c,", &gp_info.utc, &gp_info.lat, &gp_info.lat_dir, &gp_info.logt, &gp_info.logt_dir);
    
    //printf("utc = %lf , lat = %lf %c , longt = %lf %c\n\n", gp_info.utc, gp_info.lat, gp_info.lat_dir, gp_info.logt, gp_info.logt_dir);

    if(gps_callbacks.on_gps != NULL) {
        gps_callbacks.on_gps(tag, gp_info, t_len);
    }

    free(tag);
}