# _Sample project_

(See the README.md file in the upper level 'examples' directory for more information about examples.)

This is the simplest buildable example. The example is used by command `idf.py create-project`
that copies the project to user specified path and set it's name. For more information follow the [docs page](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#start-a-new-project)



## How to use example
We encourage the users to use the example as a template for the new projects.
A recommended way is to follow the instructions on a [docs page](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#start-a-new-project).

## Example folder contents

The project **sample_project** contains one source file in C language [main.c](main/main.c). The file is located in folder [main](main).

ESP-IDF projects are built using CMake. The project build configuration is contained in `CMakeLists.txt`
files that provide set of directives and instructions describing the project's source files and targets
(executable, library, or both). 

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── main
│   ├── CMakeLists.txt
│   └── main.c
└── README.md                  This is the file you are currently reading
```
Additionally, the sample project contains Makefile and component.mk files, used for the legacy Make based build system. 
They are not used or needed when building with CMake and idf.py.

## Modifications done to ESP-IDF v5.3.2
### [Yet to be officially released as a fork]

```c
esp_a2dp_api.c [Line 351]

esp_err_t esp_a2d_reconfig_samp_rate(int sample_rate) {
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_a2dp_source_ongoing_deinit) {
        return ESP_ERR_INVALID_STATE;
    }

    btc_msg_t msg;
    msg.sig = BTC_SIG_API_CALL;
    msg.pid = BTC_PID_A2DP;
    msg.act = BTC_AV_SRC_API_RECONFIG_SAMP_RATE;

    btc_av_args_t arg;
    memset(&arg, 0, sizeof(btc_av_args_t));
    arg.sample_rate = sample_rate;

    /* Switch to BTC context */
    bt_status_t stat = btc_transfer_context(&msg, &arg, sizeof(btc_av_args_t), NULL, NULL);
    return (stat == BT_STATUS_SUCCESS) ? ESP_OK : ESP_FAIL;
};

btc_av.h [Line 57]

typedef enum {
#if BTC_AV_SINK_INCLUDED
    BTC_AV_SINK_API_INIT_EVT = 0,
    BTC_AV_SINK_API_DEINIT_EVT,
    BTC_AV_SINK_API_CONNECT_EVT,
    BTC_AV_SINK_API_DISCONNECT_EVT,
    BTC_AV_SINK_API_REG_DATA_CB_EVT,
    BTC_AV_SINK_API_SET_DELAY_VALUE_EVT,
    BTC_AV_SINK_API_GET_DELAY_VALUE_EVT,
#endif  /* BTC_AV_SINK_INCLUDED */
#if BTC_AV_SRC_INCLUDED
    BTC_AV_SRC_API_INIT_EVT,
    BTC_AV_SRC_API_DEINIT_EVT,
    BTC_AV_SRC_API_CONNECT_EVT,
    BTC_AV_SRC_API_DISCONNECT_EVT,
    BTC_AV_SRC_API_REG_DATA_CB_EVT,
    BTC_AV_SRC_API_RECONFIG_SAMP_RATE,
#endif  /* BTC_AV_SRC_INCLUDED */
    BTC_AV_API_MEDIA_CTRL_EVT,
} btc_av_act_t;

btc_a2dp_source.c [Line 428]

/*****************************************************************************
**
** Function        btc_reconfig_sample_rate
**
** Description     Reconfigure source sample rate
**
** Returns
**
*******************************************************************************/
void btc_reconfig_sample_rate(int sample_rate) 
{
    src_sample_rate = sample_rate;
    bta_av_co_audio_codec_set_sample_rate(sample_rate);
    //btc_a2dp_source_setup_codec();
}

[On Top]

int src_sample_rate = 44100;



[void btc_a2dp_source_setup_codec(void)]
{
    tBTC_AV_MEDIA_FEEDINGS media_feeding;
    tBTC_AV_STATUS status;

    APPL_TRACE_EVENT("## A2DP SETUP CODEC ##\n");

    osi_mutex_global_lock();

    /* for now hardcode 44.1 khz 16 bit stereo PCM format */
    media_feeding.cfg.pcm.sampling_freq = src_sample_rate;
    media_feeding.cfg.pcm.bit_per_sample = 16;
    media_feeding.cfg.pcm.num_channel = 2;
    ...
};

bta_av_co.c [Line 1467]

/*******************************************************************************
 **
 ** Function         bta_av_co_audio_codec_set_sample_rate
 **
 ** Description      Reconfig SBC Sample rate
 **
 ** Returns          void
 **
 *******************************************************************************/
void bta_av_co_audio_codec_set_sample_rate(int sample_freq)
{
    tA2D_SBC_CIE sbc_config;
    tBTC_AV_CODEC_INFO new_cfg;

    new_cfg.id = BTC_AV_CODEC_SBC;

    sbc_config = btc_av_sbc_default_config;

    switch (sample_freq) {
    case 8000:
    case 12000:
    case 16000:
        BTC_AV_SBC_SAMP_FREQ = A2D_SBC_IE_SAMP_FREQ_16;
        sbc_config.samp_freq = A2D_SBC_IE_SAMP_FREQ_16;
        break;
    case 24000:
    case 32000:
        BTC_AV_SBC_SAMP_FREQ = A2D_SBC_IE_SAMP_FREQ_32;
        sbc_config.samp_freq = A2D_SBC_IE_SAMP_FREQ_32;
        break;
    case 48000:
        BTC_AV_SBC_SAMP_FREQ = A2D_SBC_IE_SAMP_FREQ_48;
        sbc_config.samp_freq = A2D_SBC_IE_SAMP_FREQ_48;
        break;

    case 11025:
    case 22050:
    case 44100:
        BTC_AV_SBC_SAMP_FREQ = A2D_SBC_IE_SAMP_FREQ_44;
        sbc_config.samp_freq = A2D_SBC_IE_SAMP_FREQ_44;
        break;
    default:
        APPL_TRACE_ERROR("bta_av_co_audio_set_codec PCM sampling frequency unsupported");
        break;
    }
    /* Build the codec config */
    if (A2D_BldSbcInfo(A2D_MEDIA_TYPE_AUDIO, &sbc_config, new_cfg.info) != A2D_SUCCESS) {
        APPL_TRACE_ERROR("bta_av_co_audio_set_codec A2D_BldSbcInfo failed");
    }
    
    /* The new config was correctly built */
    bta_av_co_cb.codec_cfg = new_cfg;
};

[On Top]
uint8_t BTC_AV_SBC_SAMP_FREQ = A2D_SBC_IE_SAMP_FREQ_44;

[void bta_av_co_audio_codec_reset(void)]
{
    osi_mutex_global_lock();
    FUNC_TRACE();

    /* Reset the current configuration to SBC */
    bta_av_co_cb.codec_cfg.id = BTC_AV_CODEC_SBC;
    btc_av_sbc_default_config.samp_freq = BTC_AV_SBC_SAMP_FREQ;
    ...
};


[BOOLEAN bta_av_co_audio_set_codec(const tBTC_AV_MEDIA_FEEDINGS *p_feeding, tBTC_AV_STATUS *p_status)]
{
    tA2D_SBC_CIE sbc_config;
    tBTC_AV_CODEC_INFO new_cfg;

    FUNC_TRACE();

    /* Check AV feeding is supported */
    *p_status = BTC_ERROR_SRV_AV_FEEDING_NOT_SUPPORTED;

    APPL_TRACE_DEBUG("bta_av_co_audio_set_codec cid=%d", p_feeding->format);

    /* Supported codecs */
    switch (p_feeding->format) {
    case BTC_AV_CODEC_PCM:
        new_cfg.id = BTC_AV_CODEC_SBC;

        sbc_config = btc_av_sbc_default_config;
        if ((p_feeding->cfg.pcm.num_channel != 1) &&
                (p_feeding->cfg.pcm.num_channel != 2)) {
            APPL_TRACE_ERROR("bta_av_co_audio_set_codec PCM channel number unsupported");
            return FALSE;
        }
        if ((p_feeding->cfg.pcm.bit_per_sample != 8) &&
                (p_feeding->cfg.pcm.bit_per_sample != 16)) {
            APPL_TRACE_ERROR("bta_av_co_audio_set_codec PCM sample size unsupported");
            return FALSE;
        }
        switch (p_feeding->cfg.pcm.sampling_freq) {
        case 8000:
        case 12000:
        case 16000:
            BTC_AV_SBC_SAMP_FREQ = A2D_SBC_IE_SAMP_FREQ_16;
            sbc_config.samp_freq = A2D_SBC_IE_SAMP_FREQ_16;
            break;
        case 24000:
        case 32000:
            BTC_AV_SBC_SAMP_FREQ = A2D_SBC_IE_SAMP_FREQ_32;
            sbc_config.samp_freq = A2D_SBC_IE_SAMP_FREQ_32;
            break;
        case 48000:
            BTC_AV_SBC_SAMP_FREQ = A2D_SBC_IE_SAMP_FREQ_48;
            sbc_config.samp_freq = A2D_SBC_IE_SAMP_FREQ_48;
            break;

        case 11025:
        case 22050:
        case 44100:
            BTC_AV_SBC_SAMP_FREQ = A2D_SBC_IE_SAMP_FREQ_44;
            sbc_config.samp_freq = A2D_SBC_IE_SAMP_FREQ_44;
            break;
        default:
            APPL_TRACE_ERROR("bta_av_co_audio_set_codec PCM sampling frequency unsupported");
            return FALSE;
            break;
        }
        /* Build the codec config */
        if (A2D_BldSbcInfo(A2D_MEDIA_TYPE_AUDIO, &sbc_config, new_cfg.info) != A2D_SUCCESS) {
            APPL_TRACE_ERROR("bta_av_co_audio_set_codec A2D_BldSbcInfo failed");
            return FALSE;
        }
        break;
        ...
};
```