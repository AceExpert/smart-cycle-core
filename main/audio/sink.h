#include "driver/i2s_std.h"

#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_hf_client_api.h"

void set_i2s_tx_chan(i2s_chan_handle_t* i2s_tx);
void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
void recv_audio(const uint8_t* buf, uint32_t len);
void hf_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param);