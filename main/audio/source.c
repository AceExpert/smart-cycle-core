#include <stdio.h>
#include <string.h>

//#include "driver/i2s_std.h"
#include "driver/i2s.h"

#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_hf_ag_api.h"
#include "esp_gatts_api.h"

#include "source.h"
#include "../user_data/main.h"
#include "../ble/main.h"

FILE* play_file = NULL;
FILE* cus_play_file = NULL;
struct local_playlist* playlist = NULL;
struct local_playlist* to_add_playlist = NULL;

i2s_chan_handle_t* i2s_chan = NULL;

struct disc_result_t {
    uint8_t bd_addr[6];
    int8_t rssi;
}* disc_result = NULL;

int disc_len = 0;

uint8_t hfp_on = 0;
uint8_t hfp_off = 0;
uint8_t aud_suspend = 1;

uint8_t speaker_reconfig = 0;
esp_bd_addr_t new_speaker_address;

uint8_t* speaker_addr;

char* custom_play_path = NULL;

struct source_callbacks callbacks = {
    .speaker_connected = NULL,
};

void i2s_handle_set(i2s_chan_handle_t* chan) {
    i2s_chan = chan;
}

i2s_chan_handle_t* i2s_handle_get() {
    return i2s_chan;
}

void set_on_speaker_connect(void (*callb)()) {
    callbacks.speaker_connected = callb;
}

void set_custom_play(char* path) {
    custom_play_path = path;
}

void connect_speaker() {
    speaker_addr = (uint8_t*)get_cache_field("speaker_addr");
    esp_a2d_source_connect(speaker_addr);
}

void reconfig_speaker(uint8_t* new_address) {
    adc_continuous_stop(force_adc);
    speaker_reconfig = 1;
    memcpy(new_speaker_address, new_address, 6);
    esp_hf_ag_slc_disconnect(speaker_addr);
    esp_hf_ag_audio_disconnect(speaker_addr);
    esp_a2d_source_disconnect(speaker_addr);
}

void in_call() {
    hfp_on = 1;
    printf("in call\n");
    i2s_stop(I2S_NUM_1);
    i2s_set_clk(I2S_NUM_1, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_CHANNEL_STEREO);
    i2s_start(I2S_NUM_1);
    //esp_hf_client_pcm_resample_init(16000, 16, 2);
    esp_a2d_reconfig_samp_rate(16000);
    esp_a2d_source_disconnect(speaker_addr);
    esp_a2d_source_disconnect(speaker_addr);


    /*if(aud_suspend) {
        hfp_on = 0;
        esp_a2d_reconfig_samp_rate(16000);
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    } else {
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
    }*/
}

void end_call() {
    printf("out call\n");
    hfp_on = 0;
    hfp_off = 1;
    i2s_stop(I2S_NUM_1);
    i2s_set_clk(I2S_NUM_1, 44100, I2S_DATA_BIT_WIDTH_16BIT, 2);
    i2s_start(I2S_NUM_1);
    esp_a2d_reconfig_samp_rate(44100);
    esp_a2d_source_disconnect(speaker_addr);
    esp_a2d_source_disconnect(speaker_addr);

    /*if(aud_suspend) {
        hfp_off = 0;
        esp_a2d_reconfig_samp_rate(44100);
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    } else {
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
    };*/
}

void add_playlist(const char* path, uint8_t repeat) {
    struct local_playlist* new_play = malloc(sizeof(struct local_playlist));
    new_play->play = malloc(strlen(path) + 1);
    strcpy(new_play->play, path);
    new_play->repeat = repeat;
    new_play->next = NULL;
    new_play->play_file = NULL;
    new_play->to_clear = 0;
    new_play->to_replay = 0;
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
        if((*p)->play_file && *(*p)->play_file) {
            fclose(*(*p)->play_file);
            *(*p)->play_file = NULL;
        }
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

    if(play_file && !index) {
        fclose(play_file);
        play_file = NULL;
    };
}

void safe_clear_playlist(int index) {
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
        current->to_clear = 1;
        current = current->next;
    };
}

void startup_play() {
    if(aud_suspend) {
        clear_playlist(0);
        add_playlist("/sdcard/startup.pcm", 0);
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    } else {
        safe_clear_playlist(0);
        safe_add_playlist("/sdcard/startup.pcm", 0);
    }
}

void cruise_mode() {
    if (playlist) {
        if(strcmp(playlist->play, "/sdcard/startup.pcm") == 0) {
            clear_playlist(1);
        } else {
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
            if(play_file) fclose(play_file);
            play_file = NULL;
            clear_playlist(0);
        }
    }
}

void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {

    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
        if(param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            speaker_disc_cmd(".sdisc_end", 10);
        } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            speaker_disc_cmd(".sdisc_start", 12);
        }
        break;
    }

    case ESP_BT_GAP_DISC_RES_EVT: {
        char* dev_name = NULL;
        int dev_name_len = 0;
        int8_t rssi = -126;
        uint32_t* cod = NULL;
        uint8_t* eir = NULL;
        int eir_len = 0;

        for(int i = 0; i < param->disc_res.num_prop; i++) {
            if(param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_COD) {
                cod = (uint32_t*)param->disc_res.prop[i].val;
            } else if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_RSSI) {
                rssi = *(int8_t*)param->disc_res.prop[i].val;
            } else if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_BDNAME) {
            } else if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR) {
                eir = (uint8_t*)(param->disc_res.prop[i].val);
            }
        }
        if(!cod) return;
        if(esp_bt_gap_get_cod_major_dev(*cod) == ESP_BT_COD_MAJOR_DEV_AV && eir) {
            dev_name = (char*)esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &eir_len);
            if(!dev_name) {
                dev_name = (char*)esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &eir_len);
            }
            if(!dev_name) return;
            dev_name_len = eir_len >= 100? 100 : eir_len;
            uint8_t* cmd = malloc(7 + 4 + dev_name_len + 6 + 1);
            memcpy(cmd, ".sdisc ", 7);
            memcpy(cmd + 7, &dev_name_len, 4);
            memcpy(cmd + 7 + 4, dev_name, dev_name_len);
            memcpy(cmd + 7 + 4 + dev_name_len, param->disc_res.bda, 6);
            memcpy(cmd + 7 + 4 + dev_name_len + 6, &rssi, 1);
            speaker_disc_cmd((char*)cmd, 7 + 4 + dev_name_len + 6 + 1);
        }
        break;
    }
    
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
            //esp_hf_ag_slc_connect(speaker_addr);
        }
        break;
    }
    case ESP_A2D_CONNECTION_STATE_EVT: {
        switch (param->conn_stat.state)
        {
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            if(!gatts_profile[0].connected) {
                esp_ble_gap_start_advertising(&adv_params);
            }

            if (speaker_reconfig) {
                speaker_addr = update_field("speaker_addr", new_speaker_address, 6);
                save_user_info();
                if(get_force_active() && get_cycle_state() == UNLOCKED) {
                    adc_continuous_start(force_adc);
                }
                speaker_reconfig = 0;
            }
            else if(speaker_addr == NULL) {
                speaker_addr = (uint8_t*)get_cache_field("speaker_addr");
            }
            if(callbacks.speaker_connected) callbacks.speaker_connected();
            printf("Speaker connected.\n");
            if(!hfp_on && !hfp_off) esp_hf_ag_slc_connect(speaker_addr);
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
            break;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            aud_suspend = 1;
            if(speaker_addr && !speaker_reconfig) {
                esp_a2d_source_connect(speaker_addr);
            };
            if(speaker_reconfig) {
                esp_a2d_source_connect(new_speaker_address);
            }
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
            aud_suspend = 0;
            break;
        };
        case ESP_A2D_AUDIO_STATE_SUSPEND: {
            aud_suspend = 1;
            break;
        };
        default: {
            break;
        };
        }
        break;
    }
    case ESP_A2D_MEDIA_CTRL_ACK_EVT: {
        if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_SUSPEND && param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
            /*if(hfp_on) {
                hfp_on = 0;
                esp_a2d_reconfig_samp_rate(16000);
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
            }
            else if(hfp_off) {
                hfp_off = 0;
                esp_a2d_reconfig_samp_rate(44100);
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
            }*/
        } else if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY && param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
            if(hfp_off || hfp_on) {
                hfp_off = 0;
                hfp_on = 0;
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
            } else if (i2s_handle_get()) {
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
            }
        }
        break;
    }
    case ESP_A2D_AUDIO_RECFG_EVT: {
        break;
    };
    default:
        break;
    }
};

uint32_t send_call_voice(uint8_t *buf, uint32_t len) {
    if(len == 0 || buf == NULL) return 0;
    size_t num_read;
    uint8_t data[len];
    i2s_read(I2S_NUM_1, data, len, &num_read, pdMS_TO_TICKS(10));
    //esp_hf_client_pcm_resample(data, num_read, buf);
    return len;
}

void recv_call_voice(const uint8_t *buf, uint32_t len) {
    esp_hf_ag_outgoing_data_ready();
}

int32_t send_audio(uint8_t* buf, int32_t len) {
    if (buf == NULL || len == 0) return 0;
    if(cus_play_file && custom_play_path == NULL) {
        fclose(cus_play_file);
        cus_play_file = NULL;
    } 
    if (custom_play_path) {
        if(cus_play_file == NULL)
            cus_play_file = fopen(custom_play_path, "r");
        else if (feof(cus_play_file) != 0) {
            fseek(cus_play_file, 0, SEEK_SET);
        } else {
            fread(buf, 1, len, cus_play_file);
            return len;
        }
    }
    else if (playlist) {
        if(play_file == NULL) {
            play_file = fopen(playlist->play, "r");
            playlist->play_file = &play_file;
        };
        if(feof(play_file) != 0) {
            if(playlist->repeat) {
                fseek(play_file, 0, SEEK_SET);
            } else {
                fclose(play_file);
                play_file = NULL;
                pop_playlist(&playlist);
                if(playlist) {
                    play_file = fopen(playlist->play, "r");
                    playlist->play_file = &play_file;
                } else {
                    if(i2s_chan == NULL) esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
                    return 0;
                }
            }
        }
        fread(buf, 1, len, play_file);
        return len;
    } else if (i2s_chan) {
        size_t read;
        //i2s_channel_read(*i2s_chan, buf, len, NULL, pdMS_TO_TICKS(10));
        i2s_read(I2S_NUM_1, buf, len, &read, pdMS_TO_TICKS(20));
        return len;
    }
    return 0;
};

void esp_hf_ag_cb(esp_hf_cb_event_t event, esp_hf_cb_param_t* param) {
    switch (event)
    {
    case ESP_HF_CONNECTION_STATE_EVT:
        if(param->conn_stat.state == ESP_HF_CONNECTION_STATE_SLC_CONNECTED) {
            printf("HFP Ag connected.\n");
            //esp_a2d_source_connect(speaker_addr);
            /*if(hfp_on) {
                esp_hf_ag_audio_connect(param->conn_stat.remote_bda);
                hfp_on = 0;
            }*/
        }
        if(param->conn_stat.state == ESP_HF_CONNECTION_STATE_DISCONNECTED) {
            printf("HFP Ag disconnected.\n");
            if(!speaker_reconfig) {
                esp_hf_ag_slc_connect(speaker_addr);
            };
        }
        break;

    case ESP_HF_AUDIO_STATE_EVT: {
        if (param->audio_stat.state == ESP_HF_AUDIO_STATE_CONNECTED || param->audio_stat.state == ESP_HF_AUDIO_STATE_CONNECTED_MSBC)
        {
            printf("HF AG audio connected to speaker.\n");
            esp_hf_ag_register_data_callback(recv_call_voice, send_call_voice);
        } else if (param->audio_stat.state == ESP_HF_AUDIO_STATE_DISCONNECTED) {
            //esp_hf_client_pcm_resample_deinit();
        }
        break;
    };

    case ESP_HF_BVRA_RESPONSE_EVT:
        {
            //ESP_LOGI(BT_HF_TAG, "--Voice Recognition is %s", c_vr_state_str[param->vra_rep.value]);
            break;
        }

        case ESP_HF_VOLUME_CONTROL_EVT:
        {
            //ESP_LOGI(BT_HF_TAG, "--Volume Target: %s, Volume %d", c_volume_control_target_str[param->volume_control.type], param->volume_control.volume);
            break;
        }

        case ESP_HF_UNAT_RESPONSE_EVT:
        {
            //ESP_LOGI(BT_HF_TAG, "--UNKOW AT CMD: %s", param->unat_rep.unat);
            esp_hf_ag_unknown_at_send(param->unat_rep.remote_addr, NULL);
            break;
        }

        case ESP_HF_IND_UPDATE_EVT:
        {
            //ESP_LOGI(BT_HF_TAG, "--UPDATE INDCATOR!");
            esp_hf_call_status_t call_state = 1;
            esp_hf_call_setup_status_t call_setup_state = 2;
            esp_hf_network_state_t ntk_state = 1;
            int signal = 2;
            int battery = 3;
            esp_hf_ag_ciev_report(param->ind_upd.remote_addr, ESP_HF_IND_TYPE_CALL, call_state);
            esp_hf_ag_ciev_report(param->ind_upd.remote_addr, ESP_HF_IND_TYPE_CALLSETUP, call_setup_state);
            esp_hf_ag_ciev_report(param->ind_upd.remote_addr, ESP_HF_IND_TYPE_SERVICE, ntk_state);
            esp_hf_ag_ciev_report(param->ind_upd.remote_addr, ESP_HF_IND_TYPE_SIGNAL, signal);
            esp_hf_ag_ciev_report(param->ind_upd.remote_addr, ESP_HF_IND_TYPE_BATTCHG, battery);
            break;
        }

        case ESP_HF_CIND_RESPONSE_EVT:
        {
            //ESP_LOGI(BT_HF_TAG, "--CIND Start.");
            esp_hf_call_status_t call_status = 0;
            esp_hf_call_setup_status_t call_setup_status = 0;
            esp_hf_network_state_t ntk_state = 1;
            int signal = 4;
            esp_hf_roaming_status_t roam = 0;
            int batt_lev = 3;
            esp_hf_call_held_status_t call_held_status = 0;
            esp_hf_ag_cind_response(param->cind_rep.remote_addr,call_status,call_setup_status,ntk_state,signal,roam,batt_lev,call_held_status);
            break;
        }

        case ESP_HF_COPS_RESPONSE_EVT:
        {
            const int svc_type = 1;
            esp_hf_ag_cops_response(param->cops_rep.remote_addr, "Cybertron Telecom");
            break;
        }

        case ESP_HF_CLCC_RESPONSE_EVT:
        {
            int index = 1;
            //mandatory
            esp_hf_current_call_direction_t dir = 1;
            esp_hf_current_call_status_t current_call_status = 0;
            esp_hf_current_call_mode_t mode = 0;
            esp_hf_current_call_mpty_type_t mpty = 0;
            //option
            char *number = {"123456"};
            esp_hf_call_addr_type_t type = ESP_HF_CALL_ADDR_TYPE_UNKNOWN;

            //ESP_LOGI(BT_HF_TAG, "--Calling Line Identification.");
            esp_hf_ag_clcc_response(param->clcc_rep.remote_addr, index, dir, current_call_status, mode, mpty, number, type);

            //AG shall always send ok response to HF
            //index = 0 means response ok
            index = 0;
            esp_hf_ag_clcc_response(param->clcc_rep.remote_addr, index, dir, current_call_status, mode, mpty, number, type);
            break;
        }

        case ESP_HF_CNUM_RESPONSE_EVT:
        {
            char *number = {"123456"};
            int number_type = 129;
            esp_hf_subscriber_service_type_t service_type = ESP_HF_SUBSCRIBER_SERVICE_TYPE_VOICE;
            if (service_type == ESP_HF_SUBSCRIBER_SERVICE_TYPE_VOICE || service_type == ESP_HF_SUBSCRIBER_SERVICE_TYPE_FAX) {
                //ESP_LOGI(BT_HF_TAG, "--Current Number is %s, Number Type is %d, Service Type is %s.", number, number_type, c_subscriber_service_type_str[service_type - 3]);
            } else {
                //ESP_LOGI(BT_HF_TAG, "--Current Number is %s, Number Type is %d, Service Type is %s.", number, number_type, c_subscriber_service_type_str[0]);
            }
            esp_hf_ag_cnum_response(speaker_addr, number, number_type, service_type);
            break;
        }

        case ESP_HF_VTS_RESPONSE_EVT:
        {
            //ESP_LOGI(BT_HF_TAG, "--DTMF code is: %s.", param->vts_rep.code);
            break;
        }

        case ESP_HF_NREC_RESPONSE_EVT:
        {
            //ESP_LOGI(BT_HF_TAG, "--NREC status is: %s.", c_nrec_status_str[param->nrec.state]);
            break;
        }

        case ESP_HF_ATA_RESPONSE_EVT:
        {
            //ESP_LOGI(BT_HF_TAG, "--Asnwer Incoming Call.");
            char *number = "123456";
            esp_hf_ag_answer_call(param->ata_rep.remote_addr,1,0,1,0,number,0);
            break;
        }

        case ESP_HF_CHUP_RESPONSE_EVT:
        {
            //ESP_LOGI(BT_HF_TAG, "--Reject Incoming Call.");
            char *number = "123456";
            esp_hf_ag_reject_call(param->chup_rep.remote_addr,0,0,0,0,number,0);
            break;
        }

        case ESP_HF_DIAL_EVT:
        {
            if (param->out_call.num_or_loc) {
                if (param->out_call.type == ESP_HF_DIAL_NUM) {
                    // dia_num
                    //ESP_LOGI(BT_HF_TAG, "--Dial number \"%s\".", param->out_call.num_or_loc);
                    esp_hf_ag_out_call(param->out_call.remote_addr,1,0,1,0,param->out_call.num_or_loc,0);
                } else if (param->out_call.type == ESP_HF_DIAL_MEM) {
                    // dia_mem
                    //ESP_LOGI(BT_HF_TAG, "--Dial memory \"%s\".", param->out_call.num_or_loc);
                    // AG found phone number by memory position
                    bool num_found = true;
                    if (num_found) {
                        char *number = "123456";
                        esp_hf_ag_cmee_send(param->out_call.remote_addr, ESP_HF_AT_RESPONSE_CODE_OK, ESP_HF_CME_AG_FAILURE);
                        esp_hf_ag_out_call(param->out_call.remote_addr,1,0,1,0,number,0);
                    } else {
                        esp_hf_ag_cmee_send(param->out_call.remote_addr, ESP_HF_AT_RESPONSE_CODE_CME, ESP_HF_CME_MEMORY_FAILURE);
                    }
                }
            } else {
                //dia_last
                //refer to dia_mem
                //ESP_LOGI(BT_HF_TAG, "--Dial last number.");
            }
            break;
        }
        case ESP_HF_WBS_RESPONSE_EVT:
        {
            //ESP_LOGI(BT_HF_TAG, "--Current codec: %s",c_codec_mode_str[param->wbs_rep.codec]);
            break;
        }
        case ESP_HF_BCS_RESPONSE_EVT:
        {
            //ESP_LOGI(BT_HF_TAG, "--Consequence of codec negotiation: %s",c_codec_mode_str[param->bcs_rep.mode]);
            printf("Audio codec: %d\n", param->wbs_rep.codec);
            break;
        }

    default:
        break;
    }
}

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
