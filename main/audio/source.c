#include <stdio.h>
#include <string.h>

#include "driver/i2s_std.h"

#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_hf_client_api.h"

#include "source.h"

FILE* play_file = NULL;
struct local_playlist* playlist = NULL;

i2s_chan_handle_t* i2s_chan;

static esp_bd_addr_t speaker_addr = {0x41, 0x42, 0x4a, 0x84, 0x85, 0xc2};

struct source_callbacks callbacks = {
    .speaker_connected = NULL,
};

void i2s_handle_set(i2s_chan_handle_t* chan) {
    i2s_chan = chan;
}

void set_on_speaker_connect(void (*callb)()) {
    callbacks.speaker_connected = callb;
}

void add_playlist(const char* path, uint8_t repeat) {
    struct local_playlist* new_play = malloc(sizeof(struct local_playlist));
    new_play->play = malloc(strlen(path) + 1);
    strcpy(new_play->play, path);
    new_play->repeat = repeat;
    new_play->next = NULL;
    struct local_playlist* prev_play = playlist;
    while (prev_play)
    {
        if(prev_play->next)
            prev_play = prev_play->next;
        else break;
    }
    if(prev_play) {
        prev_play->next = new_play;
    } else {
        playlist = new_play;
    }
}

void pop_playlist(struct local_playlist** p) {
    if(p && *p) {
        struct local_playlist* temp = (*p)->next;
        free((*p)->play);
        free(*p);
        *p = temp;
    }
}

void clear_playlist(int index) {
    int i = 0;
    struct local_playlist* prev = NULL;
    struct local_playlist* current = playlist;
    while (current) {
        if (i == index) {
            break;
        }
        prev = current;
        current = current->next;
        i++;
    }
    while(current) {
        pop_playlist(&current);
    }
    if(prev)
        prev->next = current;
    else
        playlist = NULL;
}

void cruise_mode() {
    if (playlist) {
        if(strcpy(playlist->play, "/sdcard/startup.pcm") == 0) {
            clear_playlist(1);
        } else {
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);
            if(play_file) fclose(play_file);
            clear_playlist(0);
        }
    }
}

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
            //esp_a2d_source_connect(speaker_addr);
        }
        break;
    }
    case ESP_A2D_CONNECTION_STATE_EVT: {
        switch (param->conn_stat.state)
        {
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            if(callbacks.speaker_connected) callbacks.speaker_connected();
            printf("Speaker connected.\n");
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
            break;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            esp_a2d_source_connect(speaker_addr);
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
    case ESP_A2D_MEDIA_CTRL_ACK_EVT: {
        if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_SUSPEND) {
        }
        break;
    }
    default:
        break;
    }
};

int32_t send_audio(uint8_t* buf, int32_t len) {
    if (buf == NULL || len == 0) return 0;
    if (playlist) {
        if(play_file == NULL) play_file = fopen(playlist->play, "r");
        if(feof(play_file) != 0) {
            if(playlist->repeat) {
                fseek(play_file, 0, SEEK_SET);
            } else {
                fclose(play_file);
                pop_playlist(&playlist);
                if(playlist) {
                    play_file = fopen(playlist->play, "r");
                } else {
                    play_file = NULL;
                    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
                    return 0;
                }
            }
        }
        return fread(buf, 1, len, play_file);
    } else if (i2s_chan) {
        i2s_channel_read(*i2s_chan, buf, len, NULL, pdMS_TO_TICKS(10));
        return len;
    }
    return 0;
};

void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    /* when connection state changed, this event comes */
    case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
        if (param->conn_stat.connected) {
            //esp_avrc_ct_send_get_rn_capabilities_cmd(0);
        } else {
            //s_avrc_peer_rn_cap.bits = 0;
        }
        break;
    }
    /* when passthrough responded, this event comes */
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT: {
        //ESP_LOGI(BT_RC_CT_TAG, "AVRC passthrough response: key_code 0x%x, key_state %d, rsp_code %d", param->psth_rsp.key_code,
        //            param->psth_rsp.key_state, param->psth_rsp.rsp_code);
        break;
    }
    /* when metadata responded, this event comes */
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        //ESP_LOGI(BT_RC_CT_TAG, "AVRC metadata response: attribute id 0x%x, %s", param->meta_rsp.attr_id, param->meta_rsp.attr_text);
        free(param->meta_rsp.attr_text);
        break;
    }
    /* when notification changed, this event comes */
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
        //ESP_LOGI(BT_RC_CT_TAG, "AVRC event notification: %d", param->change_ntf.event_id);
        //bt_av_notify_evt_handler(param->change_ntf.event_id, &param->change_ntf.event_parameter);
        break;
    }
    /* when indicate feature of remote device, this event comes */
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT: {
        //ESP_LOGI(BT_RC_CT_TAG, "AVRC remote features %"PRIx32", TG features %x", param->rmt_feats.feat_mask, param->rmt_feats.tg_feat_flag);
        break;
    }
    /* when get supported notification events capability of peer device, this event comes */
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        //ESP_LOGI(BT_RC_CT_TAG, "remote rn_cap: count %d, bitmask 0x%x", param->get_rn_caps_rsp.cap_count, param->get_rn_caps_rsp.evt_set.bits);
        //s_avrc_peer_rn_cap.bits = param->get_rn_caps_rsp.evt_set.bits;
        break;
    }
    /* when set absolute volume responded, this event comes */
    case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT: {
        //ESP_LOGI(BT_RC_CT_TAG, "Set absolute volume response: volume %d", param->set_volume_rsp.volume);
        break;
    }
    default: 
        break;

    }
}
