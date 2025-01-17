#include <stdio.h>
#include <string.h>

//#include "driver/i2s_std.h"
#include "driver/i2s.h"
#include "driver/uart.h"

#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_hf_client_api.h"

#include "sink.h"

/*i2s_chan_handle_t* tx_chan;
i2s_chan_handle_t* rx_chan;

void set_i2s_tx_chan(i2s_chan_handle_t* i2s_tx) {
    tx_chan = i2s_tx;
}

void set_i2s_rx_chan(i2s_chan_handle_t* i2s_rx) {
    rx_chan = i2s_rx;
}*/

void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    /* when authentication completed, this event comes */
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {

        }
        break;
    }
    /* when Legacy Pairing pin code requested, this event comes */
    case ESP_BT_GAP_PIN_REQ_EVT: {
        if (param->pin_req.min_16_digit) {
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        } else {
            esp_bt_pin_code_t pin_code;
            pin_code[0] = '1';
            pin_code[1] = '2';
            pin_code[2] = '3';
            pin_code[3] = '4';
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;
    }

    case ESP_BT_GAP_CFM_REQ_EVT:
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    default: 
        break;
    }
}

void esp_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {
    switch (event)
    {
    case ESP_A2D_PROF_STATE_EVT: {
        if (param->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS) {
        }
        break;
    }
    case ESP_A2D_CONNECTION_STATE_EVT: {
        switch (param->conn_stat.state)
        {
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            //i2s_channel_enable(*tx_chan);
            printf("Phone connected.\n");
            break;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            //i2s_channel_disable(*tx_chan);
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
        break;
    }
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT: {
        esp_a2d_cb_param_t* a2d_param = param;
        esp_a2d_sink_set_delay_value(a2d_param->a2d_get_delay_value_stat.delay_value + 50);
        break;
    }
    case ESP_A2D_AUDIO_CFG_EVT: {
        esp_a2d_cb_param_t* a2d_param = param;
        if (a2d_param->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
            int sample_rate = 16000;
            int ch_count = 2;
            char oct0 = a2d_param->audio_cfg.mcc.cie.sbc[0];
            if (oct0 & (0x01 << 6)) {
                sample_rate = 32000;
            } else if (oct0 & (0x01 << 5)) {
                sample_rate = 44100;
            } else if (oct0 & (0x01 << 4)) {
                sample_rate = 48000;
            }

            if (oct0 & (0x01 << 3)) {
                ch_count = 1;
            }
            /*i2s_channel_disable(*tx_chan);
            i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
            i2s_std_slot_config_t slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, ch_count);
            i2s_channel_reconfig_std_clock(*tx_chan, &clk_cfg);
            i2s_channel_reconfig_std_slot(*tx_chan, &slot_cfg);
            i2s_channel_enable(*tx_chan);*/
        }
        break;
    };
    default: {
        break;
    };
    };
};

void recv_audio(const uint8_t* buf, uint32_t len) {
    if (buf == NULL || len == 0) return;
    //i2s_channel_write(*tx_chan, buf, len, NULL, pdMS_TO_TICKS(20));
    i2s_write(I2S_NUM_1, buf, len, NULL, pdMS_TO_TICKS(10));
};

static uint32_t hf_send_audio(uint8_t *buf, uint32_t len)
{
    if(buf == NULL || len < 0) return 0;
    //int16_t data[len / 2];
    size_t num_read;
    i2s_read(I2S_NUM_0, buf, len, &num_read, pdMS_TO_TICKS(10));
    /*for(int i = 0; i < num_read / 2; i++) {
        //uint16_t temp = data[i] >> 14;
        //uint16_t final_d = (temp > INT16_MAX) ? INT16_MAX : (temp < -INT16_MAX) ? -INT16_MAX : (uint16_t)temp;
        //final_d = ((final_d & 0xff) << 8) | ((final_d & 0xff00) >> 8);
        uint8_t* ft = &temp;
        buf[i * 2] = ft[0];
        buf[i * 2 + 1] = ft[1]; 
        memcpy(buf + (i * 2), data + i, 2);
    };*/
    //i2s_channel_read(*rx_chan, buf, len, NULL, portMAX_DELAY);
    return len;
}

static void hf_recv_audio(const uint8_t *buf, uint32_t len)
{
    i2s_write(I2S_NUM_1, buf, len, NULL, pdMS_TO_TICKS(10));
    esp_hf_client_outgoing_data_ready();
}

void hf_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param) {
    switch (event)
    {
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT: {
        if(param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_CONNECTED) {
        };
        if(param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED) {
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
            printf("HFP client connected.\n");
            //esp_hf_client_connect_audio(param->conn_stat.remote_bda);

        } else if (param->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED) {
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        }
        break;
    };
    
    case ESP_HF_CLIENT_AUDIO_STATE_EVT: {
        if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED || param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC) {
            esp_hf_client_register_data_callback(hf_recv_audio, hf_send_audio);
            //i2s_channel_enable(*rx_chan);
            esp_hf_client_outgoing_data_ready();
            printf("HFP Audio connected.\n");
            uart_write_bytes(UART_NUM_2, ".incall", 7);
        } else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED) {
            uart_write_bytes(UART_NUM_2, ".endcall", 8);
            //i2s_channel_disable(*rx_chan);
        }
    }
    case ESP_HF_CLIENT_BVRA_EVT:
    {
        
        break;
    }

    case ESP_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT:
    {
        
        break;
    }

    case ESP_HF_CLIENT_CIND_ROAMING_STATUS_EVT:
    {
        
        break;
    }

    case ESP_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT:
    {
        
        break;
    }

    case ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT:
    {
        
        break;
    }

    case ESP_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT:
    {
        
        break;
    }

    case ESP_HF_CLIENT_CIND_CALL_EVT:
    {
        
        break;
    }

    case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT:
    {
        break;
    }

    case ESP_HF_CLIENT_CIND_CALL_HELD_EVT:
    {
        break;
    }

    case ESP_HF_CLIENT_BTRH_EVT:
    {
        
        break;
    }

    case ESP_HF_CLIENT_CLIP_EVT:
    {
        
        break;
    }

    case ESP_HF_CLIENT_CCWA_EVT:
    {
        
        break;
    }

    case ESP_HF_CLIENT_CLCC_EVT:
    {
        break;
    }

    case ESP_HF_CLIENT_VOLUME_CONTROL_EVT:
    {

        break;
    }

    case ESP_HF_CLIENT_AT_RESPONSE_EVT:
    {
        
        break;
    }

    case ESP_HF_CLIENT_CNUM_EVT:
    {

        break;
    }

    case ESP_HF_CLIENT_BSIR_EVT:
    {
        
        break;
    }

    case ESP_HF_CLIENT_BINP_EVT:
    {
        
        break;
    }
    default:
        break;
    }
}