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

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"

#include "lwip/sockets.h"

#include "ble/main.h"
#include "gps/gps.h"
#include "utils/main.h"

const char* WIFI_SSID[] = {"GUEST_SECURED", "STUDENT_SECURED", "CAMPUS_SECURED", "ACADEMIC_SECURED", "Academic"};
const char* WIFI_USER = "24IM10016";
const uint8_t USER_LEN = 9;
const char* WIFI_PSWD = "Anshul@7329";
const uint8_t PSWD_LEN = 11;

void cycle_locked();
void cycle_unlocked();
void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void ip_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void reconnect(TimerHandle_t timer);
void send_gps(const char* tag, struct gps_info gpsinfo);
void on_gps_connected(int phone_sock);
void on_gps_disconnected(int phone_sock);

TimerHandle_t reconnect_timer = NULL;
TimerHandle_t gps_server_auth = NULL;

int phone_socket = 0;
uint8_t phone_connected = 0;


static char* SERV_TOKEN = "auth 1234";

static struct {
    void (*on_authorized)(int sock_fd);
    void (*on_disconnected)(int sock_fd);
} gps_server_callbacks;

void gps_serv_auth(TimerHandle_t timer) {
    if(phone_connected) {
        close(phone_socket);
    }
};

void gps_server(void*) {

    struct sockaddr_in cycle_addr = {
        .sin_addr.s_addr = htonl(IPADDR_ANY),
        .sin_port = htons(3000),
        .sin_family = AF_INET,
    };

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bind(sock, (struct sockaddr*)&cycle_addr, sizeof(cycle_addr));

    listen(sock, 1);

    struct sockaddr_in phone_addr;
    socklen_t addr_len = sizeof(phone_addr);

    while(1) {
        phone_socket = accept(sock, (struct sockaddr*)&phone_addr, &addr_len);
        phone_connected = 1;
        if(gps_server_auth == NULL)
            gps_server_auth = xTimerCreate("gps_server_auth", pdMS_TO_TICKS(30000), pdFALSE, NULL, gps_serv_auth);
        xTimerReset(gps_server_auth, portMAX_DELAY);

        while(1) {
            char data[256];

            int len = recv(phone_socket, data, 256, 0);
            
            if(len == -1) {
                phone_connected = 0;
                if(xTimerIsTimerActive(gps_server_auth) != pdFALSE) {
                    xTimerStop(gps_server_auth, portMAX_DELAY);
                }
                gps_server_callbacks.on_disconnected(phone_socket);
                break;
            }

            if(xTimerIsTimerActive(gps_server_auth) != pdFALSE) {
                if(!match(SERV_TOKEN, data, 9, len)) {
                    phone_connected = 0;
                    close(phone_socket);
                    xTimerStop(gps_server_auth, portMAX_DELAY);
                    break;
                }
                xTimerStop(gps_server_auth, portMAX_DELAY);
                gps_server_callbacks.on_authorized(phone_socket);
            }
        }
    };
    vTaskDelete(NULL);
};

void start_bluetooth() {
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    esp_bluedroid_init();
    esp_bluedroid_enable();

/*
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;

    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
    esp_bt_gap_set_pin(pin_type, 0, pin_code);
*/
}

void setup_ble() {
    set_cycle_callback(LOCKED, cycle_locked);
    set_cycle_callback(UNLOCKED, cycle_unlocked);

    esp_ble_gap_register_callback(esp_ble_gap_cb);
    esp_ble_gatts_register_callback(esp_gatts_cb);
    esp_ble_gatts_app_register(0);

    esp_ble_gatt_set_local_mtu(200);
}

void setup_wifi() {
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_config);

    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    esp_wifi_set_mode(WIFI_MODE_STA);    

    esp_eap_client_set_identity((uint8_t*)WIFI_USER, USER_LEN);
    esp_eap_client_set_username((uint8_t*)WIFI_USER, USER_LEN);
    esp_eap_client_set_password((uint8_t*)WIFI_PSWD, PSWD_LEN);

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

    setup_wifi();
    start_bluetooth();
    setup_ble();
    setup_gps();
}

void cycle_unlocked() {

}

void cycle_locked() {

}

void send_gps(const char* tag, struct gps_info gpsinfo) {
    if(phone_connected) {
        send(phone_socket, tag, strlen(tag), 0);
    };
}

void on_gps_connected(int phone_sock) {
    gps_start_reading();
}

void on_gps_disconnected(int phone_sock) {
    gps_stop_reading();
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
        
        if(reconnect_timer != NULL && xTimerIsTimerActive(reconnect_timer) == pdTRUE) xTimerStop(reconnect_timer, portMAX_DELAY);

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
            for(int j = 0; j < 5; j++) {
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

        strcpy((const char*)(ap_config.sta.ssid), "CyberSky");

        if(strcmp(best_ssid, "Academic") == 0) {
            esp_wifi_sta_enterprise_disable();
        } else {
            esp_wifi_sta_enterprise_enable();
        };

        strcpy((const char*)(ap_config.sta.password), "hotspot.jio");
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