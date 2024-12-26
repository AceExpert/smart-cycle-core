#include <stdio.h>

#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_hf_client_api.h"

struct local_playlist {
    char* play;
    uint8_t repeat;
    int id;
    struct local_playlist* next;
};

int32_t send_audio(uint8_t* buf, int32_t len);
void esp_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param);
void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);
void add_playlist(const char* path, uint8_t repeat);
void clear_playlist(int index);
void cruise_mode();