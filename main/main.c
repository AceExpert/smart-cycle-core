#include <stdio.h>
#include <string.h>
#include <time.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"

#include <esp_wifi.h>
#include "esp_eap_client.h"
#include <esp_netif.h>
#include <esp_vfs.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"

#include "lwip/sockets.h"

#include "gps/gps.h"
#include "ble/main.h"
#include "utils/main.h"

i2s_chan_handle_t i2s_rx;

void cycle_locked();
void cycle_unlocked();

void start_bluetooth() {
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BTDM);

    esp_bluedroid_init();
    esp_bluedroid_enable();
}

void setup_ble() {
    set_cycle_callback(LOCKED, cycle_locked);
    set_cycle_callback(UNLOCKED, cycle_unlocked);

    esp_ble_gap_register_callback(esp_ble_gap_cb);
    esp_ble_gatts_register_callback(esp_gatts_cb);
    esp_ble_gatts_app_register(0);

    esp_ble_gatt_set_local_mtu(200);
}

void event_handler_registry() {

}

void mount_sdcard() {
    esp_vfs_fat_sdmmc_mount_config_t sdmount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t sdhost = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .miso_io_num = 19,
        .mosi_io_num = 23,
        .sclk_io_num = 18,
        .max_transfer_sz = 4000,
        .quadhd_io_num = -1,
        .quadwp_io_num = -1
    };

    spi_bus_initialize(sdhost.slot, &bus_cfg, SDSPI_DEFAULT_DMA);

    sdspi_device_config_t sddev = SDSPI_DEVICE_CONFIG_DEFAULT();
    sddev.gpio_cs = 5;
    sddev.host_id = sdhost.slot;

    sdmmc_card_t* card;

    printf("SD card mount return: %d\n", esp_vfs_fat_sdspi_mount("/sdcard", &sdhost, &sddev, &sdmount_config, &card));
    sdmmc_card_print_info(stdout, card);

}

void app_main(void)
{
    printf("Cytroid starting...\n");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_event_loop_create_default();
    esp_event_handler_register("WIFI_EVENT", ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register("IP_EVENT", ESP_EVENT_ANY_ID, ip_event_handler, NULL);
    esp_netif_init();

    esp_netif_t* sta_int = esp_netif_create_default_wifi_sta();

    i2s_chan_config_t i2s_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    i2s_std_config_t i2s_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = 12,
            .dout = 14,
            .ws = 27,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .bclk_inv = false,
                .mclk_inv = false,
                .ws_inv = false
            }
        }
    };
    i2s_new_channel(&i2s_chan_cfg, NULL, &i2s_rx);
    i2s_channel_init_std_mode(i2s_rx, &i2s_cfg);
    i2s_channel_enable(i2s_rx);

    uart_config_t main_uart = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    uart_driver_install(UART_NUM_2, 512, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_2, &main_uart);
    uart_set_pin(UART_NUM_2, 17, 16, -1, -1);

    mount_sdcard();
    start_bluetooth();
    setup_ble();
}