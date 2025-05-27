#include "stdio.h"
#include "string.h"
#include "esp_system.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_a2dp_api.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gap_bt_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_gattc_api.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/dac_oneshot.h"

#include "main.h"
#include "../utils/main.h"
#include "../user_data/main.h"
#include "../audio/source.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

TimerHandle_t auth_timer = NULL;

int setting_states = 0;

int cycle_state = LOCKED;

dac_oneshot_handle_t motor1;
dac_oneshot_handle_t motor2;

esp_bd_addr_t reset_speaker_addr;
uint8_t reset_speaker = 0;

void inform_new_network_ava(uint16_t conn_id, uint8_t avail);
void inform_new_network_size(uint16_t conn_id, int new_size, uint8_t return_none);

struct FindMy my_net = {};

struct {
    void (*locked)();
    void (*unlocked)();
    void (*force_thresh_change)();
    uint8_t (*server_connected)();
} cycle_callbacks = {NULL, NULL, NULL, NULL};

void reset_find_my() {
    my_net.phone.connected = 0;
    my_net.master_friend.connected = 0;
    my_net.master_friend.conn_id = 0;
    my_net.master_friend.net_provider = 0;
    my_net.master_friend.network_size = 0;
    for(int i = 0; i < 6; i++) {
        my_net.master_friend.addr[i] = 0;
    }
    for(int i = 0; i < 10; i++) {
        my_net.friends[i].connected = 0;
        my_net.friends[i].conn_id = 0;
        my_net.friends[i].net_provider = 0;
        my_net.friends[i].network_size = 0;
    }
    my_net.network_id = 0;
    my_net.network_available = 0;
    my_net.network_size = 1;
}

void set_motor_channels(dac_oneshot_handle_t* h1, dac_oneshot_handle_t* h2) {
    motor1 = *h1;
    motor2 = *h2;
}

void set_adc_handle(adc_continuous_handle_t adc_h) {
    force_adc = adc_h;
}

struct FindMy* get_my_network() {
    return &my_net;
};

struct gatts_prof* get_gatts_prof() {
    return gatts_profile;
};

void speaker_disc_cmd(const char* d, int len) {
    esp_ble_gatts_send_indicate(gatts_profile->gatts_if, gatts_profile->conn_id, gatts_profile->char_handle, len, (uint8_t*)d, false);
}
 
void set_cycle_callback(int state, void* callback) {
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
        break;
        
    case SERVER_CONN:
        cycle_callbacks.server_connected = callback;
        break;

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

uint8_t get_cycle_state() {
    return cycle_state;
}

void unauthorize(TimerHandle_t timer) {
    esp_ble_gatts_close(gatts_profile[0].gatts_if, gatts_profile[0].conn_id);
}

void connected_to_server(uint8_t connected) {
    mynet_update_net_ava();
    uint8_t network_ava_cmd[3] = {0x2, 0x2};
    for(int i = 0; i < 10; i++) {
        if(my_net.friends[i].connected) {
            network_ava_cmd[2] = mynet_get_net_support(my_net.friends[i].conn_id);
            esp_ble_gatts_send_indicate(gatts_profile[0].gatts_if, my_net.friends[i].conn_id, gatts_profile[0].char_handle_fr, 3, network_ava_cmd, false);
        };
    }
    if(my_net.master_friend.connected) {
        network_ava_cmd[2] = mynet_get_net_support(my_net.master_friend.conn_id);
        esp_ble_gattc_write_char(
            my_net.master_friend.itf, my_net.master_friend.conn_id, my_net.master_friend.char_handle, 
            3, network_ava_cmd, 
            ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE
        );
    }
    if(connected) {
        
    } else {

    }
};

void esp_ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        if(get_cache_field("speaker_addr") == NULL || is_speaker_connected()) {
            esp_ble_gap_start_advertising(&adv_params);
        };
        break;
    default:
        break;
    }
};

struct friend_t* mynet_get_friend(uint16_t conn_id, uint8_t* connected) {
    for(int i = 0; i < 10; i++) {
        if(my_net.friends[i].conn_id == conn_id) {
            if((connected && *connected == my_net.friends[i].connected) || connected == NULL) {
                return my_net.friends + i;
            }
        }
    }
    return NULL;
};

void mynet_add_friend(uint16_t conn_id, uint8_t* addr, int size) {
    for(int i = 0; i < 10; i++) {
        if(!my_net.friends[i].connected) {
            my_net.friends[i].conn_id = conn_id;
            memcpy(my_net.friends[i].addr, addr, 6);
            my_net.friends[i].connected = 1;
            my_net.friends[i].network_size = size;
            my_net.network_size += size;
            break;
        }
    }
}

void mynet_disconn_friend(uint16_t conn_id) {
    for(int i = 0; i < 10; i++) {
        if(my_net.friends[i].conn_id == conn_id) {
            my_net.friends[i].conn_id = 0;
            my_net.friends[i].connected = 0;
            my_net.network_size -= my_net.friends[i].network_size;
            inform_new_network_size(conn_id, 0, 0);
            connected_to_server(cycle_callbacks.server_connected());
            break;
        }
    }
}

void mynet_set_fr_size_net(uint16_t conn_id, int *size, uint8_t *net) {
    for(int i = 0; i < 10; i++) {
        if(my_net.friends[i].conn_id == conn_id && my_net.friends[i].connected) {
            if(size) {
                my_net.friends[i].network_size = *size;
            }
            if(net) {
                my_net.friends[i].net_provider = *net;
            }
            break;
        }
    }
}

void mynet_update_net_ava() {
    my_net.network_available = 0;
    for(int i = 0; i < 10; i++) {
        if(my_net.friends[i].net_provider && my_net.friends[i].connected) {
            my_net.network_available = 1;
            return;
        }
    }
    if(my_net.master_friend.connected) {
        my_net.network_available |= my_net.master_friend.net_provider;
    }
    my_net.network_available |= cycle_callbacks.server_connected();
}

uint8_t mynet_get_net_support(uint16_t conn_id) {
    uint8_t network_available = 0;
    for(int i = 0; i < 10; i++) {
        if(my_net.friends[i].conn_id != conn_id && my_net.friends[i].net_provider && my_net.friends[i].connected) {
            network_available = 1;
            return;
        }
    }
    if(my_net.master_friend.conn_id != conn_id && my_net.master_friend.connected) {
        network_available |= my_net.master_friend.net_provider;
    }
    network_available |= cycle_callbacks.server_connected();
    return network_available;
}


void inform_new_network_size(uint16_t conn_id, int new_size, uint8_t return_none) {
    uint8_t conn = 1;
    struct friend_t* frd = mynet_get_friend(conn_id, &conn);
    if(frd) {
        my_net.network_size = my_net.network_size - frd->network_size + new_size;
        frd->network_size = new_size;
    } else if (conn_id == my_net.master_friend.conn_id && my_net.master_friend.connected) {
        my_net.network_size = my_net.network_size - my_net.master_friend.network_size + new_size;
        my_net.master_friend.network_size = new_size;
    } else if (return_none) {
        return;
    }
    uint8_t network_size_cmd[6] = {0x2, 0x1};
    for(int i = 0; i < 10; i++) {
        struct friend_t* ofrd = my_net.friends + i;
        if(ofrd->conn_id != conn_id && ofrd->connected) {
            memcpy(network_size_cmd + 2, my_net.network_size - ofrd->network_size, 4);
            esp_ble_gatts_send_indicate(gatts_profile[0].gatts_if, ofrd->conn_id, gatts_profile[0].char_handle_fr, 6, network_size_cmd, false);
        }
    }

    if(my_net.master_friend.connected && conn_id != my_net.master_friend.conn_id) {
        memcpy(network_size_cmd + 2, my_net.network_size - my_net.master_friend.network_size, 4);
        esp_ble_gattc_write_char(
            my_net.master_friend.itf, my_net.master_friend.conn_id, my_net.master_friend.char_handle, 
            6, network_size_cmd, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE
        );
    }
}

void inform_new_network_ava(uint16_t conn_id, uint8_t avail) {
    uint8_t conn = 1;
    struct friend_t* frd = mynet_get_friend(conn_id, &conn);
    if(frd) {
        frd->net_provider = avail;
    } else if (conn_id == my_net.master_friend.conn_id && my_net.master_friend.connected) {
        my_net.master_friend.net_provider = avail;
    } else {
        return;
    }
    // uint8_t net_ava = my_net.network_available;
    mynet_update_net_ava();
    uint8_t network_ava_cmd[3] = {0x2, 0x2};
    for(int i = 0; i < 10; i++) {
        struct friend_t* ofrd = my_net.friends + i;
        if(ofrd->conn_id != conn_id && ofrd->connected) {
            network_ava_cmd[2] = mynet_get_net_support(ofrd->conn_id);
            esp_ble_gatts_send_indicate(gatts_profile[0].gatts_if, ofrd->conn_id, gatts_profile[0].char_handle_fr, 3, network_ava_cmd, false);
        }
    }

    if(my_net.master_friend.connected && conn_id != my_net.master_friend.conn_id) {
        network_ava_cmd[2] = mynet_get_net_support(my_net.master_friend.conn_id);
        esp_ble_gattc_write_char(
            my_net.master_friend.itf, my_net.master_friend.conn_id, my_net.master_friend.char_handle, 
            3, network_ava_cmd, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE
        );
    }
}

uint8_t mynet_is_friend(uint16_t conn_id) {
    for(int i = 0; i < 10; i++) {
        if(my_net.friends[i].conn_id == conn_id && my_net.friends[i].connected) {
            return 1;
        }
    }
    return 0;
}

void esp_gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t itf, esp_ble_gatts_cb_param_t* param) {
    switch (event)
    {
    case ESP_GATTS_REG_EVT: {
        reset_find_my();
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
        if(!match(param->disconnect.remote_bda, my_net.master_friend.addr, 6, 6)) {
            esp_ble_conn_update_params_t conn_params = {0};
            if(reset_speaker) {
                reset_speaker = 0;
            }
            esp_ble_gap_start_advertising(&adv_params);

            gatts_profile[0].connected = 1;

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
        };
    }

    case ESP_GATTS_WRITE_EVT: {
        if(param->write.handle == gatts_profile[0].descr_handle || param->write.handle == gatts_profile[0].descr_handle_fr) {
            esp_ble_gatts_send_response(itf, param->write.conn_id, param->write.trans_id, 0, NULL);
        } else if (param->write.handle == gatts_profile[0].char_handle_fr) {
            if(param->write.value[0] == 1) {
                uint16_t key_len = 0;
                memcpy(&key_len, param->write.value + 1, 2);
                uint8_t* key = malloc(key_len);
                memcpy(key, param->write.value + 3, key_len);
                
                int net_size = 0;
                memcpy(&net_size, param->write.value + 3 + key_len, 4);
                uint8_t net_ava = param->write.value[3 + key_len + 4];
                mynet_add_friend(param->write.conn_id, param->write.bda, net_size);
                inform_new_network_size(param->write.conn_id, net_size, 1);
                uint8_t network_size_cmd[6] = {0x2, 0x1};
                memcpy(network_size_cmd + 2, my_net.network_size - net_size, 4);
                esp_ble_gatts_send_indicate(itf, param->write.conn_id, gatts_profile[0].char_handle_fr, 6, network_size_cmd, false);
                uint8_t old_net_ava = my_net.network_available;
                inform_new_network_ava(param->write.conn_id, net_ava);
                uint8_t network_ava_cmd[3] = {0x2, 0x2, old_net_ava};
                esp_ble_gatts_send_indicate(itf, param->write.conn_id, gatts_profile[0].char_handle_fr, 3, network_ava_cmd, false);
                uint8_t network_id_cmd[4] = {0x2, 0x3};
                memcpy(network_id_cmd + 2, my_net.network_id, 2);
                esp_ble_gatts_send_indicate(itf, param->write.conn_id, gatts_profile[0].char_handle_fr, 4, network_id_cmd, false);
            } else if (param->write.value[0] == 2) {
                if(param->write.value[1] == 1) {
                    int net_size = 0;
                    memcpy(&net_size, param->write.value + 2, 4);
                    inform_new_network_size(param->write.conn_id, net_size, 1);
                    struct friend_t* joined_friend = mynet_get_friend(param->write.conn_id, NULL);
                } else if (param->write.value[1] == 2) {
                    uint8_t net_ava = param->write.value[2];
                    inform_new_network_ava(param->write.conn_id, net_ava);

                    if(my_net.network_available) {

                    } else {

                    }
                } else if (param->write.value[1] == 3) {

                }
            }
        } else if (param->write.handle == gatts_profile[0].char_handle) {

            struct split_result res[10];
            int size = split(param->write.value, param->write.len, ' ', res);
            
            if (xTimerIsTimerActive(auth_timer) != pdFALSE) {
                if(size == 2) {
                    if(match("auth", res[0].text, 4, res[0].len) && match(TOKEN, res[1].text, 20, res[1].len)) {
                        memcpy(my_net.phone.addr, param->write.bda, 6);
                        my_net.phone.conn_id = param->write.conn_id;
                        my_net.phone.connected = 1;
                    } else {
                        esp_ble_gatts_close(gatts_profile[0].gatts_if, param->write.conn_id);
                        
                    }
                } else esp_ble_gatts_close(gatts_profile[0].gatts_if, param->write.conn_id);
                xTimerStop(auth_timer, portMAX_DELAY);
            } else if (param->write.conn_id == my_net.phone.conn_id) {
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
                        reset_speaker = 1;
                        memcpy(reset_speaker_addr, res[2].text, 6);
                        esp_ble_gatts_close(itf, param->write.conn_id);
                    } else if (match("user_name", res[1].text, 9, res[1].len)) {
                        update_field("user", (uint8_t*)res[2].text, res[2].len);
                        save_user_info();
                    } else if (match("phone_token", res[1].text, 11, res[1].len)) {
                        update_field("phone_token", (uint8_t*)res[2].text, res[2].len);
                        save_user_info();
                    } else if (match("gps_token", res[1].text, 9, res[1].len)) {
                        update_field("gps_token", (uint8_t*)res[2].text, res[2].len);
                        save_user_info();
                    } else if (match("set_alarm", res[1].text, 9, res[1].len)) {
                        update_field("alarm", (uint8_t*)res[2].text, 1);
                        save_user_info();
                    } else if (match("set_total", res[1].text, 9, res[1].len)) {
                        res[2].text = realloc(res[2].text, res[2].len + 1);
                        res[2].text[res[2].len] = 0;
                        sscanf(res[2].text, "%d", &setting_states);
                    } else if (match("set_state", res[1].text, 9, res[1].len)) {
                        res[2].text = realloc(res[2].text, res[2].len + 1);
                        res[2].text[res[2].len] = 0;
                        update_field(res[2].text, (uint8_t*)res[3].text, res[3].len);
                        if((--setting_states) == 0) {
                            adc_continuous_stop(force_adc);
                            save_user_info();
                            cycle_callbacks.force_thresh_change();
                            if(cycle_state == UNLOCKED && get_force_active()) {
                                adc_continuous_start(force_adc);
                            }
                        }
                    } else if (match("speaker_setup", res[1].text, 13, res[1].len)) {
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

                    } else if (match("destination", res[1].text, 11, res[1].len)) {

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

        gatts_profile[0].char_uuid_fr.len = ESP_UUID_LEN_16;
        gatts_profile[0].char_uuid_fr.uuid.uuid16 = 0xff02;

        esp_ble_gatts_start_service(gatts_profile[0].service_handle);

        esp_ble_gatts_add_char(
            gatts_profile[0].service_handle, 
            &gatts_profile->char_uuid, 
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 
            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
            &char_value,
            NULL        
        );

        esp_ble_gatts_add_char(
            gatts_profile[0].service_handle, 
            &gatts_profile->char_uuid_fr, 
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 
            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
            &char_value,
            NULL        
        );
        break;
    }

    case ESP_GATTS_ADD_CHAR_EVT: {
        switch (param->add_char.char_uuid.uuid.uuid16) {
            case 0xff01: {
                gatts_profile[0].char_handle = param->add_char.attr_handle;
                gatts_profile[0].descr_uuid.len = ESP_UUID_LEN_16;
                gatts_profile[0].descr_uuid.uuid.uuid16 = 0x3333;

                esp_ble_gatts_add_char_descr(gatts_profile[0].service_handle, &gatts_profile[0].descr_uuid,
                ESP_GATT_PERM_WRITE | ESP_GATT_PERM_READ, NULL, NULL
                );
                break;
            };

            case 0xff02: {
                gatts_profile[0].char_handle_fr = param->add_char.attr_handle;
                gatts_profile[0].descr_uuid_fr.len = ESP_UUID_LEN_16;
                gatts_profile[0].descr_uuid_fr.uuid.uuid16 = 0x3334;

                esp_ble_gatts_add_char_descr(gatts_profile[0].service_handle, &gatts_profile[0].descr_uuid_fr,
                ESP_GATT_PERM_WRITE | ESP_GATT_PERM_READ, NULL, NULL
                );
                break;
            };

            default: {
                break;
            }
        };
        break;
    };

    case ESP_GATTS_ADD_CHAR_DESCR_EVT: {
        if(param->add_char_descr.descr_uuid.uuid.uuid16 == 0x3333) {
            gatts_profile[0].descr_handle = param->add_char_descr.attr_handle;
        } else {
            gatts_profile[0].descr_handle_fr = param->add_char_descr.attr_handle;
        };
        break;
    }

    case ESP_GATTS_DISCONNECT_EVT: {
        if(!match(param->disconnect.remote_bda, my_net.master_friend.addr, 6, 6)) {
            if(param->disconnect.conn_id == my_net.phone.conn_id) {
                my_net.phone.connected = 0;
                my_net.phone.conn_id = 0;
                gatts_profile[0].connected = 0;
                cycle_state = LOCKED;
                if(cycle_callbacks.locked != NULL) {
                    cycle_callbacks.locked();
                }
                if(reset_speaker) {
                    esp_ble_gap_stop_advertising();
                    reconfig_speaker(reset_speaker_addr);
                } else {
                    esp_ble_gap_start_advertising(&adv_params);
                };
                send_uart_cmd(UART_NUM_2, ".audio_disconn\n", 15);
                break;
            } else if (mynet_is_friend(param->disconnect.conn_id)) {
                mynet_disconn_friend(param->disconnect.conn_id);
            };
        }

        break;
    }

    default:
        break;
    }
};

void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t itf, esp_ble_gattc_cb_param_t* param) {
    switch (event)
    {
    case ESP_GATTC_REG_EVT:
        my_net.master_friend.itf = itf;
        break;
    
    case ESP_GATTC_CONNECT_EVT:
        if(match(param->connect.remote_bda, my_net.master_friend.addr, 6, 6)) {
            esp_ble_gap_stop_advertising();
        };
        
        break;

    case ESP_GATTC_DIS_SRVC_CMPL_EVT:
        esp_ble_gap_start_advertising(&adv_params);

        esp_gattc_service_elem_t services[5];
        uint16_t count = 5;

        esp_ble_gattc_get_service(itf, param->dis_srvc_cmpl.conn_id, NULL, services, &count, 0);
        my_net.master_friend.start_handle = services[2].start_handle;
        my_net.master_friend.end_handle = services[2].end_handle;
        
        count = 1;
        esp_gattc_char_elem_t charac;
        esp_gattc_descr_elem_t descr;

        esp_ble_gattc_get_all_char(itf, param->dis_srvc_cmpl.conn_id, my_net.master_friend.start_handle, my_net.master_friend.end_handle, &charac, &count, 1);

        printf("charac cnt: %d\n", count);
        printf("Charac: %d\n", charac.properties);
        my_net.master_friend.char_handle = charac.char_handle;
        count = 1;

        esp_ble_gattc_get_all_descr(itf, param->dis_srvc_cmpl.conn_id, my_net.master_friend.char_handle, &descr, &count, 0);

        printf("descr cnt: %d\n", count);

        my_net.master_friend.descr_handle = descr.handle;

        esp_ble_gattc_register_for_notify(itf, my_net.master_friend.addr, my_net.master_friend.char_handle);
        break;

    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        uint16_t notify = 1;
        esp_ble_gattc_write_char_descr(
            itf, 
            my_net.master_friend.conn_id, 
            my_net.master_friend.descr_handle, 
            sizeof(notify), (uint8_t*)&notify, 
            ESP_GATT_WRITE_TYPE_RSP, 
            ESP_GATT_AUTH_REQ_NONE
        );
        break;
    }

    case ESP_GATTC_NOTIFY_EVT:
        if(param->notify.handle == my_net.master_friend.char_handle) {
            if(param->notify.value[0] == 1) {
                uint16_t key_len = 0;
                memcpy(&key_len, param->notify.value + 1, 2);
                uint8_t* key = malloc(key_len);
                memcpy(key, param->notify.value + 3, key_len);
                
                int net_size = 0;
                memcpy(&net_size, param->notify.value + 3 + key_len, 4);
                uint8_t net_ava = param->notify.value[3 + key_len + 4];
                mynet_add_friend(param->notify.conn_id, param->notify.remote_bda, net_size);
                inform_new_network_size(param->notify.conn_id, net_size, 1);
                uint8_t network_id_cmd[4] = {0x2, 0x3};
                memcpy(network_id_cmd + 2, my_net.network_id, 2);
            } else if (param->notify.value[0] == 2) {
                if(param->notify.value[1] == 1) {
                    int net_size = 0;
                    memcpy(&net_size, param->notify.value + 2, 4);
                    inform_new_network_size(param->notify.conn_id, net_size, 1);
                    struct friend_t* joined_friend = mynet_get_friend(param->notify.conn_id, NULL);
                } else if (param->notify.value[1] == 2) {
                    uint8_t net_ava = param->notify.value[2];
                    inform_new_network_ava(param->notify.conn_id, net_ava);

                    if(my_net.network_available) {

                    } else {

                    }
                } else if (param->notify.value[1] == 3) {

                }
            }
        }
        break;
    
    case ESP_GATTC_DISCONNECT_EVT:
        if(match(param->connect.remote_bda, my_net.master_friend.addr, 6, 6)) {
            for(int i = 0; i < 6; i++) {
                my_net.master_friend.addr[i] = 0;
            }
            my_net.master_friend.connected = 0;
            my_net.master_friend.conn_id = 0;
            my_net.network_size -= my_net.master_friend.network_size;
            mynet_update_net_ava();
            inform_new_network_size(my_net.master_friend.conn_id, 0, 0);
            connected_to_server(cycle_callbacks.server_connected());
        };

        break;

    default:
        break;
    }
};