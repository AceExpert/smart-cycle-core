#include <stdio.h>
#include <math.h>
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

#include "esp_bt.h"
#include "esp_bt_main.h"

#include "driver/gpio.h"
//#include "driver/i2s_std.h"
#include "driver/i2s.h"
#include "driver/i2c_master.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"

#include "lwip/sockets.h"

#include "gps/gps.h"
#include "utils/main.h"
#include "audio/sink.h"

#define MPU_VALUE(b2, b1) ((int16_t)(((b2) >> 7) ? ~((b2) << 8 | (b1)) : ((b2) << 8 | (b1))))
#define ABS(a) (((a) > 0) ? (a) : (-(a)))

/*i2s_chan_handle_t i2s_tx;
i2s_chan_handle_t mic_rx;*/

TaskHandle_t* uart_task;

i2c_master_bus_handle_t mpu_bus;
i2c_master_dev_handle_t mpu_handle;

uint8_t mpu_addr[5] = {0x6B, 0x1C, 0x3B, 0x1B, 0x43};

time_t alert_time = 0;
time_t turb_time = 0;
time_t turb_stop = 0;
uint8_t to_alert = 0;

clock_t acc_start_time = 0;
clock_t acc_time = 0;
clock_t acc_stop = 0;

uint8_t cruise = 0;
uint8_t unlocked = 0;
uint8_t probe_done = 0;

uint8_t audio_start = 0;
uint8_t call_start = 0;

const char* WIFI_SSID[] = {"GUEST_SECURED", "VS", "LBS", "STUDENT_SECURED", "CAMPUS_SECURED", "ACADEMIC_SECURED", "Academic"};
const char* WIFI_USER = "24IM10016";
const uint8_t USER_LEN = 9;
const char* WIFI_PSWD = "Anshul@7329";
const uint8_t PSWD_LEN = 11;

void cycle_locked();
void cycle_unlocked();
void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void ip_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void reconnect(TimerHandle_t timer);
void send_gps(const char* tag, struct gps_info gpsinfo, int tlen);
void on_gps_connected(int phone_sock);
void on_gps_disconnected(int phone_sock);
void uart_cmd_task(void*);
void process_cmd(const char*);
void audio_state_set_cb(uint8_t aud, uint8_t in_call);

TimerHandle_t reconnect_timer = NULL;
TimerHandle_t alert_timer = NULL;
TimerHandle_t gps_alive = NULL;

int server_socket = 0;
uint8_t server_connected = 0;
uint8_t server_connecting = 0;

static char* SERV_TOKEN = "hMrXDM0x6G";

static struct {
    void (*on_authorized)(int sock_fd);
    void (*on_disconnected)(int sock_fd);
} gps_server_callbacks;

int get_max(int* arr, size_t len) {
    int max = *arr;
    for(int i = 1; i < len; i++) {
        if(arr[i] > max) max = arr[i];
    }
    return max;
}

void gps_serv_alive(TimerHandle_t timer) {
    if(server_connected) {
        close(server_socket);
        server_connected = 0;
        server_connecting = 0;
        server_socket = 0;
        gps_server_callbacks.on_disconnected(0);
    }
    if(xTimerIsTimerActive(gps_alive) != pdFALSE) {
        xTimerStop(gps_alive, portMAX_DELAY);
    }
};

void send_server_cmd(const char* cmd, const char* param) {
    int len = strlen(cmd), sent = 0;

    char* full_cmd = malloc(strlen(cmd) + 1 + strlen(SERV_TOKEN) + 1 + strlen(param) + 1);
    memcpy(full_cmd, cmd, strlen(cmd));

    full_cmd[len++] = ' ';
    memcpy(full_cmd + len, SERV_TOKEN, strlen(SERV_TOKEN));
    len += strlen(SERV_TOKEN);

    if(strlen(param)) {
        full_cmd[len++] = ' ';

        memcpy(full_cmd + len, param, strlen(param));
        len += strlen(param);
    };
    if(full_cmd[len - 1] != '\n') {
        full_cmd[len++] = '\n';
    }

    if(server_connected) {
        //while(sent < len)
        //    sent += send(server_socket, full_cmd + sent, len - sent, 0);
        send(server_socket, full_cmd, len, 0);
    }
    free(full_cmd);
}

/* GPS Server with UDP Multicast support but this doesn't work on campus network
void gps_server(void*) {

    send_uart_cmd(UART_NUM_2, ".state\n", 7);
    
    struct sockaddr_in cycle_addr = {
        .sin_addr.s_addr = htonl(IPADDR_ANY),
        .sin_port = htons(3000),
        .sin_family = AF_INET,
    };
    struct sockaddr_in udp_m_addr = {
        .sin_addr.s_addr = htonl(IPADDR_ANY),
        .sin_port = htons(5007),
        .sin_family = AF_INET,
    };

    int udp_multi = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

    int flags = fcntl(sock, F_GETFL);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(udp_multi, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bind(sock, (struct sockaddr*)&cycle_addr, sizeof(cycle_addr));
    bind(udp_multi, (struct sockaddr*)&udp_m_addr, sizeof(udp_m_addr));

    uint8_t ttl = 1;
    setsockopt(udp_multi, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(uint8_t));

    struct ip_mreq imreq = {0};
    struct in_addr iaddr = {0};

    imreq.imr_interface.s_addr = htonl(IPADDR_ANY);
    inet_aton("239.0.1.2", &imreq.imr_multiaddr.s_addr);

    setsockopt(udp_multi, IPPROTO_IP, IP_MULTICAST_IF, &iaddr, sizeof(struct in_addr));
    setsockopt(udp_multi, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq, sizeof(struct ip_mreq));

    listen(sock, 3);

    struct sockaddr_in phone_addr;
    socklen_t addr_len = sizeof(phone_addr);
    
    char data[257];

    while(1) {

        struct timeval timeout = {
            .tv_sec = 7,
            .tv_usec = 0,
        };

        struct fd_set rds;
        struct fd_set wds;
        FD_ZERO(&rds);
        FD_ZERO(&wds);
        FD_SET(udp_multi, &rds);
        if(!server_connected) {
            FD_SET(sock, &rds);
        } else {
            FD_SET(server_socket, &rds);
            if(phone_auth && to_alert) {
                FD_SET(server_socket, &wds);
            }
        };
    
        int socks[3] = {sock, udp_multi, server_socket};
        if(select(get_max(socks, server_connected? 3 : 2) + 1, &rds, &wds, NULL, &timeout) > 0) {
        
            if(FD_ISSET(udp_multi, &rds) != 0) {
                printf(" s3\n");

                struct sockaddr_in saddr;
                socklen_t saddr_len = sizeof(saddr);

                char udp_data[256];
                int len = recvfrom(udp_multi, udp_data, 256, 0, (struct sockaddr*)&saddr, &saddr_len);
                if(len && match("cytroid gps", udp_data, 11, len)) {
                    sendto(udp_multi, "cytroid gps", 11, 0, (struct sockaddr*)&saddr, saddr_len);
                }
            }

            if(!server_connected) {
                if(FD_ISSET(sock, &rds) != 0) {
                    server_socket = accept(sock, (struct sockaddr*)&phone_addr, &addr_len);
                    server_connected = 1;
                    phone_auth = 0;
                    flags = fcntl(server_socket, F_GETFL);
                    fcntl(server_socket, F_SETFL, flags | O_NONBLOCK);
                    if(gps_server_auth == NULL)
                        gps_server_auth = xTimerCreate("gps_server_auth", pdMS_TO_TICKS(30000), pdFALSE, NULL, gps_serv_auth);
                    xTimerReset(gps_server_auth, portMAX_DELAY);
                }
            } else {

                if(FD_ISSET(server_socket, &rds) != 0) {
                    int dlen = recv(server_socket, data, 256, 0);
                    data[dlen] = 0;
                    printf("%d: %s\n", dlen, data);
                    if (dlen <= 0) {
                        server_connected = 0;
                        phone_auth = 0;
                        if(xTimerIsTimerActive(gps_server_auth) != pdFALSE)
                            xTimerStop(gps_server_auth, portMAX_DELAY);
                        if(xTimerIsTimerActive(gps_alive) != pdFALSE) {
                            xTimerStop(gps_alive, portMAX_DELAY);
                        }
                        gps_server_callbacks.on_disconnected(server_socket);
                    } else if(!phone_auth) {
                        if(!match(SERV_TOKEN, data, 9, dlen)) {
                            server_connected = 0;
                            phone_auth = 0;
                            close(server_socket);
                            if(xTimerIsTimerActive(gps_server_auth) != pdFALSE)
                                xTimerStop(gps_server_auth, portMAX_DELAY);
                            if(xTimerIsTimerActive(gps_alive) != pdFALSE) {
                                xTimerStop(gps_alive, portMAX_DELAY);
                            }
                        } else {
                            phone_auth = 1;
                            if(xTimerIsTimerActive(gps_server_auth) != pdFALSE)
                                xTimerStop(gps_server_auth, portMAX_DELAY);
                            gps_server_callbacks.on_authorized(server_socket);
                        }
                    } else {
                        if(match(".heartbeat", data, 10, dlen)) {
                            if(gps_alive != NULL) {
                                if(xTimerIsTimerActive(gps_alive) != pdFALSE) {
                                    xTimerStop(gps_alive, portMAX_DELAY);
                                }
                            }
                        }
                    }
                };
                
                if (FD_ISSET(server_socket, &wds) != 0) {
                    if(to_alert) {
                        to_alert = 0;
                        send(server_socket, "$alert\n", 7, 0);
                    }
                }
            }
        }
    };
    vTaskDelete(NULL);
};*/

//GPS Server through my VPS
void gps_server(void*) {
    
    send_uart_cmd(UART_NUM_2, ".state\n", 7);

    struct sockaddr_in server_addr = {
        .sin_addr.s_addr = htonl(IPADDR_ANY),
        .sin_port = htons(3121),
        .sin_family = AF_INET,
    };

    inet_aton("209.74.79.245", &server_addr.sin_addr.s_addr);
    
    char data[257];
    int sock = 0;

    while (1)
    {
        if(unlocked) {
            if(sock) {
                close(sock);
                server_socket = 0;
                sock = 0;
            }
            server_connected = 0;
            server_connecting = 0;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        struct timeval timeout = {
            .tv_sec = 7,
            .tv_usec = 0,
        };

        struct fd_set rds;
        struct fd_set wds;
        FD_ZERO(&rds);
        FD_ZERO(&wds);
        
        if(!server_connected && !server_connecting) {
            if(sock) close(sock);
            sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

            int flags = fcntl(sock, F_GETFL);
            fcntl(sock, F_SETFL, flags | O_NONBLOCK);

            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            socklen_t addr_len = sizeof(server_addr);
            int res = connect(sock, (struct sockaddr*)&server_addr, addr_len);
            if(res < 0 && errno != EINPROGRESS) {
                server_connected = 0;
                server_connecting = 0;
                continue;
            } else if (res != 0) {
                server_connecting = 1;
                FD_SET(sock, &wds);
            }
        } else if (server_connecting) {
            FD_SET(sock, &wds);
        } else {
            FD_SET(sock, &rds);
        };
        if(!server_connected && !server_connecting) continue;
        if(select(sock + 1, &rds, &wds, NULL, &timeout) > 0) {
            if(FD_ISSET(sock, &wds) != 0) {
                if(server_connecting) {
                    server_connecting = 0;

                    int result;
                    socklen_t res_len = sizeof(result);
                    getsockopt(sock, SOL_SOCKET, SO_ERROR, &result, &res_len);

                    if(result == 0) {
                        
                        int tok_len = strlen(SERV_TOKEN);
                        char authcmd[30 + tok_len + 1]; 
                        memcpy(authcmd, "$auth cycle ff:bc:cd:ff:ff:aa ", 30);
                        memcpy(authcmd + 30, SERV_TOKEN, tok_len);
                        authcmd[30 + tok_len] = '\n';
                        send(sock, authcmd, 30 + tok_len + 1, 0);
                        server_socket = sock;
                        server_connected = 1;
                        if(to_alert) {
                            to_alert = 0;
                            send_server_cmd("$alert", "");
                        };
                        gps_server_callbacks.on_authorized(server_socket);
                        if(gps_alive == NULL) 
                            gps_alive = xTimerCreate("gps_alive_timer", pdMS_TO_TICKS(300000), pdFALSE, NULL, gps_serv_alive);
                        xTimerReset(gps_alive, portMAX_DELAY);
                    } else {
                        server_connected = 0;
                    }
                }
            }

            if(FD_ISSET(sock, &rds) != 0) {
                int dlen = recv(server_socket, data, 256, 0);
                data[dlen] = 0;

                if(dlen <= 0) {
                    close(sock);
                    server_connected = 0;
                    server_connecting = 0;
                    sock = 0;
                    server_socket = 0;
                    gps_server_callbacks.on_disconnected(server_socket);
                } else {

                    if(gps_alive == NULL) 
                        gps_alive = xTimerCreate("gps_alive_timer", pdMS_TO_TICKS(300000), pdFALSE, NULL, gps_serv_alive);
                    xTimerReset(gps_alive, portMAX_DELAY);
                };
            }
        }
    }
    
}

void start_bluetooth() {
    set_audio_state_cb(audio_state_set_cb);
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);

    esp_bluedroid_init();
    esp_bluedroid_enable();

    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;

    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    esp_bt_gap_set_device_name("Cytroid Speaker");

    esp_bt_gap_register_callback(bt_gap_cb);

    esp_a2d_register_callback(esp_a2d_cb);
    esp_a2d_sink_register_data_callback(recv_audio);
    esp_hf_client_register_callback(hf_client_cb);
    
    esp_a2d_sink_init();
    esp_hf_client_init();

    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);

}

void audio_state_set_cb(uint8_t aud, uint8_t in_call) {
    audio_start = aud;
    call_start = in_call;
}

void setup_wifi() {
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_config);

    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    esp_wifi_set_mode(WIFI_MODE_STA);    

    esp_eap_client_set_identity((uint8_t*)WIFI_USER, USER_LEN);
    esp_eap_client_set_username((uint8_t*)WIFI_USER, USER_LEN);
    esp_eap_client_set_password((uint8_t*)WIFI_PSWD, PSWD_LEN);

    esp_eap_client_set_ttls_phase2_method(ESP_EAP_TTLS_PHASE2_MSCHAPV2);

    esp_wifi_start();
}

void setup_gps() {
    set_on_gps(send_gps);
    gps_server_callbacks.on_authorized = on_gps_connected;
    gps_server_callbacks.on_disconnected = on_gps_disconnected;
    gps_start();
    xTaskCreate(gps_server, "gps_server", 4096, NULL, 4, NULL);
};

void event_handler_registry() {

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

    /*i2s_chan_config_t i2s_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    i2s_chan_config_t i2s_rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);

    i2s_std_config_t i2s_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = 19,
            .dout = 18,
            .ws = 5,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .bclk_inv = false,
                .mclk_inv = false,
                .ws_inv = false
            }
        }
    };
    i2s_std_config_t i2s_rx_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = 14,
            .din = 13,
            .ws = 12,
            .dout = I2S_GPIO_UNUSED,
            .invert_flags = {
                .bclk_inv = false,
                .mclk_inv = false,
                .ws_inv = false
            }
        }
    };
    i2s_new_channel(&i2s_chan_cfg, &i2s_tx, NULL);
    i2s_new_channel(&i2s_rx_chan_cfg, NULL, &mic_rx);

    i2s_channel_init_std_mode(i2s_tx, &i2s_cfg);
    i2s_channel_init_std_mode(mic_rx, &i2s_rx_cfg);*/

    i2s_config_t i2s_mic_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t i2s_mic_pins = {
        .bck_io_num = 14,
        .ws_io_num = 12,
        .data_out_num = -1,
        .data_in_num = 13,
        .mck_io_num = -1,
    };

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 1024,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t i2s_pins = {
        .bck_io_num = 19,
        .ws_io_num = 18,
        .data_out_num = 5,
        .data_in_num = -1,
        .mck_io_num = -1,
    };

    i2s_driver_install(I2S_NUM_0, &i2s_mic_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &i2s_mic_pins);

    i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_1, &i2s_pins);

    i2s_stop(I2S_NUM_0);
    i2s_stop(I2S_NUM_1);

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

    i2c_master_bus_config_t mpu_bus_cfg = {
        .clk_source = I2C_CLK_SRC_APB,
        .scl_io_num = 22,
        .sda_io_num = 21,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
        .i2c_port = -1,
    };

    i2c_device_config_t mpu_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x68,
        .scl_speed_hz = 100000,
    };

    i2c_new_master_bus(&mpu_bus_cfg, &mpu_bus);
    i2c_master_bus_add_device(mpu_bus, &mpu_cfg, &mpu_handle);
    
    uint8_t reset[2] = {*mpu_addr, 0};
    uint8_t cfg[2] = {mpu_addr[1], 0b00010000};
    uint8_t g_cfg[2] = {mpu_addr[3], 0b00001000};

    i2c_master_transmit(mpu_handle, reset, 2, -1);
    i2c_master_transmit(mpu_handle, cfg, 2, -1);
    //i2c_master_transmit(mpu_handle, g_cfg, 2, -1);

    //set_i2s_tx_chan(&i2s_tx);
    //set_i2s_rx_chan(&mic_rx);
    setup_wifi();
    start_bluetooth();
    setup_gps();

    xTaskCreate(uart_cmd_task, "uart_cmd_", 3584, NULL, 4, uart_task);
}

void send_gps(const char* tag, struct gps_info gpsinfo, int tlen) {
    if(server_connected) {
        char param[tlen+1];
        memcpy(param, tag, tlen);
        param[tlen] = 0;
        send_server_cmd("$gps", param);
    };
}

void send_alert(TimerHandle_t timer) {
    if(server_connected) {
        send_server_cmd("$alert", "");
        xTimerStop(timer, portMAX_DELAY);
    };
}

void on_gps_connected(int phone_sock) {
    //gps_start_reading();
}

void on_gps_disconnected(int phone_sock) {
    //gps_stop_reading();
}

void process_cmd(const char* cmd) {
    if (strcmp(cmd, "ack") == 0) {
        acked();
    } else if(strcmp(cmd, "alert") == 0) {
        if(server_connected) {
            send_server_cmd("$alert", "");
        } else {
            to_alert = 1;
        };
    } else if (strcmp(cmd, "unlocked") == 0) {
        probe_done = 1;
        unlocked = 1;
        if(reconnect_timer != NULL && xTimerIsTimerActive(reconnect_timer) != pdFALSE) xTimerStop(reconnect_timer, portMAX_DELAY);
        esp_wifi_disconnect();
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        if(audio_start) {
            send_uart_cmd(UART_NUM_2, ".audio_play\n", 12);
        }
        if(call_start) {
            send_uart_cmd(UART_NUM_2, ".incall\n", 8);
        }
    } else if (strcmp(cmd, "locked") == 0) {
        probe_done = 1;
        unlocked = 0;
        cruise = 0;
        gps_start_reading();
        gps_stop_reading();
        if(reconnect_timer == NULL)
            reconnect_timer = xTimerCreate("reconn_timer", pdMS_TO_TICKS(9000), pdFALSE, NULL, reconnect);
        xTimerReset(reconnect_timer, portMAX_DELAY);
    } else if (strcmp(cmd, "next") == 0) {
        esp_hf_client_answer_call();
    } else if (strcmp(cmd, "prev") == 0) {

    } else if (strcmp(cmd, "play") == 0) {
        esp_hf_client_reject_call();
    } else if (strcmp(cmd, "vol_up") == 0) {
    
    } else if (strcmp(cmd, "vol_down") == 0) {

    } else if (strcmp(cmd, "vol_stop") == 0) {

    } else if (strcmp(cmd, "audio_conn") == 0) {
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    } else if (strcmp(cmd, "audio_disconn") == 0) {
        esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    }
    else {
        struct split_result parts[5];
        int len = split((unsigned const char*)cmd, strlen(cmd), ' ', parts);
        if(len == 2 && match("audio_connect", parts[0].text, 13, parts[0].len)) {
            esp_a2d_sink_connect((uint8_t*)parts[1].text);
            esp_hf_client_connect((uint8_t*)parts[1].text);
        };
        for(int i = 0; i < len; i++) free(parts[i].text);
    }
}

struct gyro_reading {
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_Z;
    clock_t time;
} gyro_last = {0, 0, 0, 0};

struct gyro_change {
    int gyro_x;
    int gyro_y;
    int gyro_z;
} net_gyro_change = {0, 0, 0};

struct gyro_change gyro_start = {0, 0, 0};

void uart_cmd_task(void*) {
    uint8_t raw[6];

    uint8_t* uart_cmd = malloc(0);
    int cmd_len = 0;
    
    uint8_t cmd_start = 0;

    while(1) {

        uint8_t d;
        int read_len = uart_read_bytes(UART_NUM_2, &d, 1, 1 / portTICK_PERIOD_MS);
        if(read_len) {
            if(d == '\n') printf("\\n");
            printf("%c", d);
        }
        
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
                if(strcmp((const char*)uart_cmd, "ack") != 0)
                    uart_write_bytes(UART_NUM_2, ".ack\n", 5);
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
        if(!cruise && 0) {
            int16_t accel[3];
            if(i2c_master_transmit_receive(mpu_handle, mpu_addr + 2, 1, raw, 6, -1) == 0) {

                *accel = MPU_VALUE((int16_t)raw[0], (int16_t)raw[1]) * 10 / 4096.00; 
                accel[1] = MPU_VALUE((int16_t)raw[2], (int16_t)raw[3]) * 10 / 4096.00;
                accel[2] = MPU_VALUE((int16_t)raw[4], (int16_t)raw[5]) * 10 / 4096.00;

                double net_a = sqrt(pow(accel[0], 2) + pow(accel[1], 2) + pow(accel[2], 2));
                if (ABS(net_a - 10.5) >= 3) {
                    if(alert_time) {
                        alert_time = time(NULL);
                    }
                    else if (turb_time) {
                        turb_stop = 0;
                        if ((time(NULL) - turb_time) >= (unlocked ? 1 : 2)) {
                            if(unlocked) {
                                cruise = 1;
                                send_uart_cmd(UART_NUM_2, ".cruise\n", 8);
                            } else {
                                process_cmd("alert");
                                gps_start_reading();
                                send_uart_cmd(UART_NUM_2, ".alert\n", 7);
                                printf("alert\n");
                                alert_time = time(NULL);
                            };
                        };
                    } else {
                        turb_time = time(NULL);
                        turb_stop = 0;
                    }
                } else {
                    if (alert_time) {
                        if (time(NULL) - alert_time >= 9) {
                            turb_stop = 0;
                            turb_time = 0;
                            alert_time = 0;
                            send_uart_cmd(UART_NUM_2, ".alert_stop\n", 12);
                            gps_stop_reading();
                            printf("alert stop\n");
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
        };

        if(cruise && 0) {
            int16_t gyro[3];
            if(i2c_master_transmit_receive(mpu_handle, mpu_addr + 4, 1, raw, 6, -1) == 0) {

                *gyro = MPU_VALUE((int16_t)raw[0], (int16_t)raw[1]) / 65.50;
                gyro[1] = MPU_VALUE((int16_t)raw[2], (int16_t)raw[3]) / 65.50;
                gyro[2] = MPU_VALUE((int16_t)raw[4], (int16_t)raw[5]) / 65.50;

                
            };
        }

        if(!cmd_start)
            vTaskDelay(pdMS_TO_TICKS(10));

    };
    vTaskDelete(NULL);
    free(uart_cmd);
}

void ip_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    switch (event_id)
    {
    case IP_EVENT_STA_GOT_IP:
        ip_event_got_ip_t* ip_evt_data = event_data;
        printf("IP: "IPSTR"\n", IP2STR(&(ip_evt_data->ip_info.ip)));
        break;
    
    default:
        break;
    }
}

void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    switch (event_id)
    {
    case WIFI_EVENT_STA_CONNECTED: {
        
        if(reconnect_timer != NULL && xTimerIsTimerActive(reconnect_timer) != pdFALSE) xTimerStop(reconnect_timer, portMAX_DELAY);

        printf("Wifi connected\n");
        break;
    }    
    case WIFI_EVENT_SCAN_DONE: {

        wifi_event_sta_scan_done_t* scan_res = event_data;
        uint16_t num = scan_res->number;

        char best_ssid[32] = "";
        int rssi_best = -200;
        
        wifi_ap_record_t ap_records[scan_res->number];
        esp_wifi_scan_get_ap_records(&num, ap_records);
        
        for(int i = 0; i < num; i++) {
            //printf("%s: RSSI: %d\n", ap_records[i].ssid, ap_records[i].rssi);            
            for(int j = 0; j < 7; j++) {
                if(strcmp((const char*)(ap_records[i].ssid), "GUEST_SECURED") == 0 && ap_records[i].rssi > -100) {
                    strcpy(best_ssid, "GUEST_SECURED");
                    break;
                }
                if(strcmp((const char*)(ap_records[i].ssid), WIFI_SSID[j]) == 0) {
                    if(ap_records[i].rssi > rssi_best) {
                        strcpy(best_ssid, WIFI_SSID[j]);
                        rssi_best = ap_records[i].rssi;
                    };
                    break;
                };
            };
        };

        wifi_config_t ap_config = {
            .sta = {
                .rm_enabled = 1,
                .btm_enabled = 1,
                .mbo_enabled = 1,
                .ft_enabled = 1,
                .failure_retry_cnt = 3,
            },
        };

        strcpy((char*)(ap_config.sta.ssid), best_ssid);

        if(strcmp(best_ssid, "Academic") == 0) {
            esp_wifi_sta_enterprise_disable();
        } else {
            esp_wifi_sta_enterprise_enable();
        };

        strcpy((char*)(ap_config.sta.password), WIFI_PSWD);

        strcpy((char*)ap_config.sta.ssid, "SputhNet");
        strcpy((char*)ap_config.sta.password, "hotspot.sputh");
        esp_wifi_sta_enterprise_disable();

        esp_wifi_set_config(WIFI_IF_STA, &ap_config);
        esp_wifi_connect();

        break;
    };

    case WIFI_EVENT_STA_DISCONNECTED: {
        
        wifi_event_sta_disconnected_t* disconn = event_data;
        printf("Wifi disconnected Reason: %d\n", disconn->reason);

        if(disconn->reason == WIFI_REASON_ROAMING) {
            printf("Due to roaming\n");
        } else {
            if(reconnect_timer == NULL)
                reconnect_timer = xTimerCreate("reconn_timer", pdMS_TO_TICKS(9000), pdFALSE, NULL, reconnect);
            if(!unlocked)
                xTimerReset(reconnect_timer, portMAX_DELAY);
        }
        break;
    }

    case WIFI_EVENT_STA_START:
        esp_wifi_scan_start(NULL, false);
        break;
    default:
        break;
    }
};

void reconnect(TimerHandle_t timer) {
    esp_wifi_scan_start(NULL, false);
};