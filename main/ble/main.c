#include "stdio.h"
#include "string.h"
#include "esp_system.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gap_bt_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/dac_oneshot.h"

#include "main.h"
#include "../utils/main.h"
#include "../user_data/main.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

TimerHandle_t auth_timer = NULL;

int cycle_state = LOCKED;

dac_oneshot_handle_t motor1;
dac_oneshot_handle_t motor2;

struct {
    void (*locked)();
    void (*unlocked)();
    void (*force_thresh_change)();
} cycle_callbacks = {NULL, NULL, NULL};

void set_motor_channels(dac_oneshot_handle_t* h1, dac_oneshot_handle_t* h2) {
    motor1 = *h1;
    motor2 = *h2;
}

struct gatts_prof* get_gatts_prof() {
    return gatts_profile;
}
 
void set_cycle_callback(int state, void (*callback)()) {
    switch (state)
    {
    case LOCKED:
        cycle_callbacks.locked = callback;
        break;
    
    case UNLOCKED:
        cycle_callbacks.unlocked = callback;
        break;

    case THRESH:
        cycle_callbacks.force_thresh_change = callback;
        
    default:
        break;
    }
}

void direction_indic(int8_t dynam, uint8_t m1, uint8_t m2) {
    switch (dynam)
    {
    case 0: {
        dac_oneshot_output_voltage(motor1, m1? 255 : 0);
        dac_oneshot_output_voltage(motor2, m2? 255 : 0);
        break;
    }

    case 1:{
        break;
    }

    case -1: {
        dac_oneshot_output_voltage(motor1, 0);
        dac_oneshot_output_voltage(motor2, 0);
        break;
    }

    default:
        break;
    };
}

void unauthorize(TimerHandle_t timer) {
    esp_ble_gatts_close(gatts_profile[0].gatts_if, gatts_profile[0].conn_id);
}

void esp_ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
    default:
        break;
    }
};

void esp_gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t itf, esp_ble_gatts_cb_param_t* param) {
    switch (event)
    {
    case ESP_GATTS_REG_EVT: {
        gatts_profile[0].gatts_if = itf;
        gatts_profile[0].app_id = param->reg.app_id;
        gatts_profile[0].service_id.id.inst_id = 0;
        gatts_profile[0].service_id.is_primary = 1;
        gatts_profile[0].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gatts_profile[0].service_id.id.uuid.uuid.uuid16 = 0x00ff;

        esp_ble_gap_set_device_name("Cytroid Dragon's Breathe");
        
        esp_ble_gatts_create_service(itf, &gatts_profile->service_id, 4);

        esp_ble_gap_config_adv_data(&adv_data);
        esp_ble_gap_config_adv_data(&scan_rsp_data);

        break;
    };

    case ESP_GATTS_CONNECT_EVT: {
        esp_ble_conn_update_params_t conn_params = {0};
        
        esp_ble_gap_stop_advertising();

        gatts_profile[0].conn_id = param->connect.conn_id;
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.latency = 0;
        conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
        conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
        conn_params.timeout = 400;    // timeout = 400*10ms = 4000ms
        //start sent the update connection parameters to the peer device.
        
        esp_ble_gap_update_conn_params(&conn_params);

        if(auth_timer == NULL)
            auth_timer = xTimerCreate("auth_timer", pdMS_TO_TICKS(7000), pdFALSE, NULL, unauthorize);
        xTimerReset(auth_timer, portMAX_DELAY);
        
        break;
    }

    case ESP_GATTS_WRITE_EVT: {
        if(param->write.handle == gatts_profile[0].descr_handle) {
            esp_ble_gatts_send_response(itf, param->write.conn_id, param->write.trans_id, 0, NULL);
        } else {
            struct split_result res[10];
            int size = split(param->write.value, param->write.len, ' ', res);
            
            if (xTimerIsTimerActive(auth_timer) != pdFALSE) {
                if(size == 2) {
                    if(match("auth", res[0].text, 4, res[0].len) && match(TOKEN, res[1].text, 20, res[1].len)) {
                    } else {
                        esp_ble_gatts_close(gatts_profile[0].gatts_if, gatts_profile[0].conn_id);
                        
                    }
                } else esp_ble_gatts_close(gatts_profile[0].gatts_if, gatts_profile[0].conn_id);
                xTimerStop(auth_timer, portMAX_DELAY);
            } else {
                if(size >= 2 && match(TOKEN, res[0].text, 20, res[0].len)) {
                    if(match("unlock", res[1].text, 6, res[1].len)) {
                        cycle_state = UNLOCKED;
                        if(cycle_callbacks.unlocked != NULL) {
                            cycle_callbacks.unlocked();

                        }
                    } else if(match("lock", res[1].text, 4, res[1].len)) {
                        cycle_state = LOCKED;
                        if(cycle_callbacks.locked != NULL) {
                            cycle_callbacks.locked();
                        }
                    } 
                    else if (match("speaker_addr", res[1].text, 12, res[1].len)) {
                        update_field("speaker_addr", (uint8_t*)res[2].text, 6);
                        save_user_info();
                    } else if (match("user_name", res[1].text, 9, res[1].len)) {
                        update_field("user", (uint8_t*)res[2].text, res[2].len);
                        save_user_info();
                    } else if (match("phone_token", res[1].text, 11, res[1].len)) {
                        update_field("phone_token", (uint8_t*)res[2].text, res[2].len);
                        save_user_info();
                    } else if (match("gps_token", res[1].text, 9, res[1].len)) {
                        update_field("gps_token", (uint8_t*)res[2].text, res[2].len);
                        save_user_info();
                    } else if (match("force_sense", res[1].text, 9, res[1].len)) {
                        update_field("force_sense", (uint8_t*)res[2].text, res[2].len);
                        save_user_info();
                        if(cycle_callbacks.force_thresh_change != NULL) {
                            cycle_callbacks.force_thresh_change();
                        }
                    } else if (match("set_alarm", res[1].text, 9, res[1].len)) {
                        update_field("alarm", (uint8_t*)res[2].text, 1);
                        save_user_info();
                    } else if (match("speaker_setup", res[1].text, 13, res[1].len)) {
                        printf("Starting speaker setup\n");
                        esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 0x10, 10);
                    }
                    else if (match("left_turn", res[1].text, 9, res[1].len)) {
                        direction_indic(0, 1, 0);
                    }
                    else if (match("right_turn", res[1].text, 10, res[1].len)) {
                        direction_indic(0, 0, 1);
                    } 
                    else if (match("u_turn", res[1].text, 6, res[1].len)) {
                    
                    } 
                    else if (match("ne_turn", res[1].text, 7, res[1].len)) {
                    
                    } 
                    else if (match("nw_turn", res[1].text, 7, res[1].len)) {

                    }
                    else if (match("se_turn", res[1].text, 7, res[1].len)) {

                    }
                    else if (match("sw_turn", res[1].text, 7, res[1].len)) {

                    } else if (match("dir_stop", res[1].text, 8, res[1].len)) {
                        direction_indic(-1, 0, 0);

                    }else if (match("destination", res[1].text, 11, res[1].len)) {

                    } else if (match("audio_connect", res[1].text, 13, res[1].len)) {
                        char cmd[22] = ".audio_connect ";
                        strcat(cmd, (const char*)param->write.bda);
                        cmd[21] = '\n';
                        send_uart_cmd(UART_NUM_2, cmd, 22);
                    } else if (match("audio_disconn", res[1].text, 13, res[1].len)) {
                        send_uart_cmd(UART_NUM_2, ".audio_disconn\n", 15);
                    }
                };
            }

            for(int j = 0; j < size; j++) free(res[j].text);
        }
        break;
    }

    case ESP_GATTS_CREATE_EVT: {
        gatts_profile[0].service_handle = param->create.service_handle;
        gatts_profile[0].char_uuid.len = ESP_UUID_LEN_16;
        gatts_profile[0].char_uuid.uuid.uuid16 = 0xff01;

        esp_ble_gatts_start_service(gatts_profile[0].service_handle);

        esp_ble_gatts_add_char(
            gatts_profile[0].service_handle, 
            &gatts_profile->char_uuid, 
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 
            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
            &char_value,
            NULL        
        );
        break;
    }

    case ESP_GATTS_ADD_CHAR_EVT: {
        gatts_profile[0].char_handle = param->add_char.attr_handle;
        gatts_profile[0].descr_uuid.len = ESP_UUID_LEN_16;
        gatts_profile[0].descr_uuid.uuid.uuid16 = 0x3333;

        esp_ble_gatts_add_char_descr(gatts_profile[0].service_handle, &gatts_profile[0].descr_uuid,
        ESP_GATT_PERM_WRITE | ESP_GATT_PERM_READ, NULL, NULL
        );
        break;
    };

    case ESP_GATTS_ADD_CHAR_DESCR_EVT: {
        gatts_profile[0].descr_handle = param->add_char_descr.attr_handle;
        break;
    }

    case ESP_GATTS_DISCONNECT_EVT: {
        cycle_state = LOCKED;
        if(cycle_callbacks.locked != NULL) {
            cycle_callbacks.locked();
        }
        esp_ble_gap_start_advertising(&adv_params);
        send_uart_cmd(UART_NUM_2, ".audio_disconn\n", 15);
        break;
    }

    default:
        break;
    }
};
