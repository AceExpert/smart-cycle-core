#include <stdio.h>
#include "time.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"

#include <esp_vfs.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_continuous.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"

#include "ble/main.h"
#include "utils/main.h"
#include "audio/source.h"

#define MPU_VALUE(b2, b1) ((int16_t)(((b2) >> 7) ? ~((b2) << 8 | (b1)) : ((b2) << 8 | (b1))))
#define ABS(a) (((a) > 0) ? (a) : -(a))

i2s_chan_handle_t i2s_rx;
i2c_master_bus_handle_t mpu_bus;
i2c_master_dev_handle_t mpu_handle;

adc_continuous_handle_t adc_handle;

uint8_t mpu_addr[3] = {0x6B, 0x1C, 0x3B};

TaskHandle_t* motion_task;

time_t alert_time = 0;
time_t turb_time = 0;
time_t turb_stop = 0;

uint8_t unlocked = 0;
uint8_t cruise = 0;

clock_t force_start[2] = {0, 0};
clock_t tap_end[2] = {0, 0};
clock_t last_tap[2] = {0, 0};
int taps[2] = {0, 0};

enum MEDIA_CONTROLS {
    PREV = 0,
    NEXT,
    PLAY_PAUSE,
    VOL_UP,
    VOL_DOWN,
    VOL_STOP,
    JOIN,
};

struct media_cmd {
    int ctrl;
    double value;
    struct media_cmd* next;
};

struct media_cmd* media_cmds = NULL;

void add_media(struct media_cmd** media_q, int ctrl, double value) {
    if(media_q) {
        struct media_cmd* new_media = malloc(sizeof(struct media_cmd));
        new_media->ctrl = ctrl;
        new_media->value = value;
        new_media->next = NULL;

        if(*media_q) {
            (*media_q)->next = new_media;
        } else {
            *media_q = new_media;
        }
    }
}

void pop_media(struct media_cmd** media_q) {
    if(media_q && *media_q) {
        struct media_cmd* temp = (*media_q)->next;
        free(*media_q);
        *media_q = temp;
    }
}

void cycle_locked();
void cycle_unlocked();
void monitor_motion(void*);
void media_ctrl_exec(void*);
bool adc_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *data, void *user_data);

void start_bluetooth() {
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BTDM);

    esp_bluedroid_init();
    esp_bluedroid_enable();
    
    esp_bt_gap_set_device_name("Cytroid-BR");
    esp_bt_gap_register_callback(bt_gap_cb);
    
    esp_avrc_ct_init();
    esp_avrc_ct_register_callback(bt_app_rc_ct_cb);

    esp_avrc_rn_evt_cap_mask_t evt_set = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
    ESP_ERROR_CHECK(esp_avrc_tg_set_rn_evt_cap(&evt_set));

    esp_a2d_register_callback(esp_a2d_cb);
    esp_a2d_source_register_data_callback(send_audio);

    esp_a2d_source_init();

    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
}

void setup_ble() {
    set_cycle_callback(LOCKED, cycle_locked);
    set_cycle_callback(UNLOCKED, cycle_unlocked);

    esp_ble_gap_register_callback(esp_ble_gap_cb);
    esp_ble_gatts_register_callback(esp_gatts_cb);
    esp_ble_gatts_app_register(0);

    esp_ble_gatt_set_local_mtu(200);
}

void setup_adc() {
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = 1024,
        .conv_frame_size = SOC_ADC_DIGI_DATA_BYTES_PER_CONV,
        .flags.flush_pool = 1,
    };

    adc_continuous_new_handle(&handle_cfg, &adc_handle);

    adc_continuous_config_t adc_cfg = {
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .sample_freq_hz = 20000,
        .pattern_num = 1,
    };

    adc_digi_pattern_config_t adc_pattern[2] = {0};

    for (int i = 1; i < 2; i++) {
        uint8_t adc_unit, adc_channel;
        adc_continuous_io_to_channel(i? 36 : 39, &adc_unit, &adc_channel); 
        adc_pattern[0].atten = ADC_ATTEN_DB_12;
        adc_pattern[0].channel = adc_channel;
        adc_pattern[0].unit = adc_unit;
        adc_pattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    };

    adc_cfg.adc_pattern = adc_pattern;
    adc_continuous_config(adc_handle, &adc_cfg);

    adc_continuous_evt_cbs_t adc_cbs = {
        .on_conv_done = adc_cb,
    };

    adc_continuous_register_event_callbacks(adc_handle, &adc_cbs, NULL);
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

    gpio_set_direction(GPIO_NUM_27, GPIO_MODE_OUTPUT);
    gpio_set_direction(14, GPIO_MODE_OUTPUT);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    i2s_chan_config_t i2s_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
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
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(UART_NUM_2, 512, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_2, &main_uart);
    uart_set_pin(UART_NUM_2, 17, 16, -1, -1);

    i2c_master_bus_config_t mpu_bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .scl_io_num = 22,
        .sda_io_num = 21,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
        .i2c_port = 0,
    };

    i2c_device_config_t mpu_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x68,
        .scl_speed_hz = 100000,
    };

    /*i2c_new_master_bus(&mpu_bus_cfg, &mpu_bus);
    i2c_master_bus_add_device(mpu_bus, &mpu_cfg, &mpu_handle);
    
    uint8_t reset[2] = {*mpu_addr, 0};
    uint8_t cfg[2] = {mpu_addr[1], 0b00010000};

    i2c_master_transmit(mpu_handle, reset, 2, -1);
    i2c_master_transmit(mpu_handle, cfg, 2, -1);*/

    setup_adc();
    //mount_sdcard();
    start_bluetooth();
    setup_ble();

    adc_continuous_start(adc_handle);

    //xTaskCreate(monitor_motion, "motion_monitor", 1024*3, NULL, 5, motion_task);
    xTaskCreate(media_ctrl_exec, "adc_read", 1024*3, NULL, 5, NULL);
}

void cycle_unlocked() {
    unlocked = 1;
    uart_write_bytes(UART_NUM_2, ".unlocked\n", 10);
    add_playlist("/sdcard/startup.pcm", 0);
    add_playlist("/sdcard/standby.pcm", 1);
    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    adc_continuous_start(adc_handle);
}

void cycle_locked() {
    unlocked = 0;
    cruise = 0;
    i2s_handle_set(NULL);
    uart_write_bytes(UART_NUM_2, ".locked\n", 8);
    adc_continuous_stop(adc_handle);
}

void process_cmd(const char* cmd) {
    if(strcmp(cmd, "audio_play") == 0) {
        if(!cruise) {
            cruise = 1;
            cruise_mode();
        } else {
            clear_playlist(0);
        }
        i2s_handle_set(&i2s_rx);
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    }
}

void monitor_motion(void*) {
    uint8_t raw[6];

    uint8_t* uart_cmd = malloc(0);
    int cmd_len = 0;
    
    uint8_t cmd_start = 0;

    while(1) {

        if(unlocked) {
            uint8_t d;
            int read_len = uart_read_bytes(UART_NUM_2, &d, 1, 5 / portTICK_PERIOD_MS);
            
            if(cmd_start) {
                if (read_len < 1) {
                    cmd_start = 0;
                    free(uart_cmd);
                    uart_cmd = malloc(0);
                    cmd_len = 0;
                }
                else if (d == '\n') {
                    cmd_start = 0;
                    uart_cmd = realloc(uart_cmd, cmd_len+1);
                    uart_cmd[cmd_len] = 0;
                    cmd_len = 0;
                    process_cmd((const char*)uart_cmd);
                    free(uart_cmd);
                    uart_cmd = malloc(0);
                } else {
                    uart_cmd = realloc(uart_cmd, cmd_len+1);
                    uart_cmd[cmd_len++] = d;
                }
            }
            if (read_len && d == '.') {
                cmd_start = 1;
            }
        };

        if(!cruise) {
            int16_t accel[3];
            i2c_master_transmit_receive(mpu_handle, mpu_addr + 2, 1, raw, 6, -1);

            *accel = MPU_VALUE((int16_t)raw[0], (int16_t)raw[1]) * 10 / 4096.00; 
            accel[1] = MPU_VALUE((int16_t)raw[2], (int16_t)raw[3]) * 10 / 4096.00;
            accel[2] = MPU_VALUE((int16_t)raw[4], (int16_t)raw[5]) * 10 / 4096.00;

            double net_a = sqrt(pow(accel[0], 2) + pow(accel[1], 2) + pow(accel[2], 2));

            if (ABS(net_a - 10) >= 0.85) {
                if(alert_time) {
                    alert_time = time(NULL);
                }
                else if (turb_time) {
                    if (time(NULL) - turb_time >= (unlocked ? 1 : 2)) {
                        if(unlocked) {
                            cruise_mode();
                            cruise = 1;
                        } else {
                            add_playlist("/sdcard/alarm.pcm", 1);
                            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
                            uart_write_bytes(UART_NUM_2, ".alert\n", 7);
                            alert_time = time(NULL);
                        };
                    };
                } else {
                    turb_time = time(NULL);
                }
            } else {
                if (alert_time) {
                    if (time(NULL) - alert_time >= 9) {
                        turb_stop = 0;
                        turb_time = 0;
                        alert_time = 0;
                        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
                        clear_playlist(0);
                    }
                } 
                else if (turb_stop) {
                    if (time(NULL) - turb_stop >= 3) {
                        turb_stop = 0;
                        turb_time = 0;
                        alert_time = 0;
                    }
                }
                else if (turb_time) {
                    turb_stop = time(NULL);
                }
            }
        };

        if(cruise) {
            
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(uart_cmd);
    vTaskDelete(NULL);
}

double avg(adc_digi_output_data_t* readings, uint32_t len) {
    long sum = 0;
    for(int i = 0; i < len; i++) sum += readings[i].type1.data;
    return (double)sum / len;
}

void media_ctrl_exec(void*){
    while(1) {
        while(media_cmds) {
            switch (media_cmds->ctrl)
            {
            case PREV:
                esp_ble_gatts_send_indicate(gatts_profile->gatts_if, gatts_profile->conn_id, gatts_profile->descr_handle, 5, ".prev", false);
                break;
            
            case NEXT:
                esp_ble_gatts_send_indicate(gatts_profile->gatts_if, gatts_profile->conn_id, gatts_profile->descr_handle, 5, ".next", false);

                break;

            case PLAY_PAUSE:
                esp_ble_gatts_send_indicate(gatts_profile->gatts_if, gatts_profile->conn_id, gatts_profile->descr_handle, 5, ".play", false);

                break;

            case VOL_UP:
                esp_ble_gatts_send_indicate(gatts_profile->gatts_if, gatts_profile->conn_id, gatts_profile->descr_handle, 7, ".vol_up", false);

                break;

            case VOL_DOWN:
                esp_ble_gatts_send_indicate(gatts_profile->gatts_if, gatts_profile->conn_id, gatts_profile->descr_handle, 9, ".vol_down", false);

                break;

            case VOL_STOP:
                esp_ble_gatts_send_indicate(gatts_profile->gatts_if, gatts_profile->conn_id, gatts_profile->descr_handle, 9, ".vol_stop", false);

                break;

            default:
                break;
            }
            pop_media(&media_cmds);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

bool adc_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *data, void *user_data)
{
    adc_digi_output_data_t* final_data = data->conv_frame_buffer;

    double final_val = final_data->type1.data;

    int gpio_num;
    adc_continuous_channel_to_io(ADC_UNIT_1, final_data->type1.channel, &gpio_num);

    int i = (int)(gpio_num == 39);
    int j = (int)(gpio_num != 39);

    if(final_val < 3801 && !force_start[i]) {
        tap_end[i] = 0;
        force_start[i] = clock();
    } else if (clock() - force_start[i] >= 400 && final_val < 3801) {
        add_media(&media_cmds, i? VOL_UP : VOL_DOWN, final_val);
    };

    if(tap_end[i] && clock() - tap_end[i] >= 280 && !force_start[i]) {
        if(taps[i] <= 2 && !taps[j]) { 
            if(taps == 1)
                add_media(&media_cmds, i? NEXT : PREV, 0);
            tap_end[i] = 0;
            taps[i] = 0;
        } else if (taps[i] == 1 && taps[j] == 1) {
            add_media(&media_cmds, PLAY_PAUSE, 0);
            tap_end[i] = 0;
            taps[i] = 0;
            tap_end[j] = 0;
            taps[j] = 0;
        };
    }

    if(force_start[i] && final_val > 4000 && !tap_end[i]) {
        clock_t diff = clock() - force_start[i];
        if(diff <= 300 && diff >= 15) {
            taps[i]++;
            tap_end[i] = clock();
        } else if (diff >= 400) {
            add_media(&media_cmds, VOL_STOP, 0);
        }
        force_start[i] = 0;
    }
    return false;
}