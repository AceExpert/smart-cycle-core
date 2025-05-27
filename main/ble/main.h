#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gap_bt_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_gattc_api.h"

#include "driver/dac_oneshot.h"
#include "esp_adc/adc_continuous.h"

static char* TOKEN = "49n5pEsOUDF25rBhUFmN";

static uint8_t adv_service_uuid128[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static adc_continuous_handle_t force_adc = NULL;

static struct gatts_prof {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t gatts_master_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    uint16_t char_handle_fr;
    esp_bt_uuid_t char_uuid_fr;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
    uint16_t descr_handle_fr;
    esp_bt_uuid_t descr_uuid_fr;
    uint8_t connected;
} gatts_profile[1] = {
    {
        .gatts_if = ESP_GATT_IF_NONE,
        .connected = 0,
    }
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x00C2,
    .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    //.min_interval = 0x0006,
    //.max_interval = 0x0010,
    .appearance = 0x00C2,
    .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_attr_value_t char_value = {.attr_max_len = 200, .attr_len = 0, .attr_value = NULL};

struct friend_t {
    uint16_t conn_id;
    uint8_t addr[6];
    uint8_t connected;
    uint8_t net_provider;
    int network_size;
};

struct master_friend_t {
    uint16_t conn_id;
    uint8_t addr[6];
    uint8_t connected;
    uint8_t net_provider;
    int network_size;
    uint16_t start_handle; 
    uint16_t end_handle;
    esp_gatt_if_t itf; 
    uint16_t conn_id;
    uint16_t conn_handle;
    uint16_t char_handle;
    uint16_t descr_handle;
};

struct FindMy {
    struct {
        uint16_t conn_id;
        uint8_t addr[6];
        uint8_t connected;
    } phone;
    struct master_friend_t master_friend;
    struct friend_t friends[10];

    uint16_t network_id;
    uint8_t network_available;
    int network_size;
};

enum CycleState {
    LOCKED = 0,
    UNLOCKED = 1,
    THRESH = 2,
    SERVER_CONN = 3,
};

void esp_ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param);
void esp_gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t itf, esp_ble_gatts_cb_param_t* param);
void set_cycle_callback(int state, void *callback);
struct gatts_prof* get_gatts_prof();
void set_motor_channels(dac_oneshot_handle_t* h1, dac_oneshot_handle_t* h2);
void speaker_disc_cmd(const char* d, int len);
void set_adc_handle(adc_continuous_handle_t adc_h);
uint8_t get_cycle_state();
struct FindMy* get_my_network();
void connected_to_server(uint8_t connected);