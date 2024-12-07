#include <stdio.h>

#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_hf_client_api.h"

void recv_audio(const uint8_t* buf, uint32_t len);
void esp_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param);