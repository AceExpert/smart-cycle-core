#include <stdio.h>

#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_hf_client_api.h"

void esp_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {
    switch (event)
    {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        switch (param->conn_stat.state)
        {
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            
            break;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:

            break;
        default: {
            break;
        }
        }
        break;
    };

    case ESP_A2D_AUDIO_STATE_EVT: {
        switch (param->audio_stat.state)
        {
        case ESP_A2D_AUDIO_STATE_STARTED: {
            
            break;
        };
        case ESP_A2D_AUDIO_STATE_SUSPEND: {
            
            break;
        };
        default: {
            break;
        };
        }
    }
    default:
        break;
    }
};

void recv_audio(const uint8_t* buf, uint32_t len) {

};