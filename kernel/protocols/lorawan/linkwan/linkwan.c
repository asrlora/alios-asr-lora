/*
 * Copyright (C) 2015-2017 Alibaba Group Holding Limited
 */
#include "commissioning.h"
#include "utilities.h"
#include "LoRaMacCrypto.h"
#include "LoRaMac.h"
#include "LoRaMacClassB.h"
#include "timeServer.h"
#include "hw.h"
#include "low_power.h"
#include "radio.h"
#include "linkwan_ica_at.h"
#include <uart_port.h>
#include "hal/soc/flash.h"
#include "hal/soc/uart.h"
#include <aos/aos.h>
#ifdef AOS_KV
#include <assert.h>
#include "kvmgr.h"
#endif
#include "lwan_config.h"  
#include "linkwan.h"

#define MAX_BEACON_RETRY_TIMES 2
#define LORA_KEYS_MAGIC_NUM 0xABABBABA 

static uint8_t tx_buf[LORAWAN_APP_DATA_BUFF_SIZE];
static lora_AppData_t tx_data = {tx_buf, 1, 10};
static uint8_t rx_buf[LORAWAN_APP_DATA_BUFF_SIZE];
static lora_AppData_t rx_data = {rx_buf, 0, 0};

static LoRaMacPrimitives_t LoRaMacPrimitives;
static LoRaMacCallback_t LoRaMacCallbacks;
static LoRaMainCallback_t *app_callbacks;

static volatile bool next_tx = true;
static volatile bool rejoin_flag = true;

static uint8_t gGatewayID[3] ={0};
static uint8_t g_beacon_retry_times = 0;

static uint32_t g_ack_index = 0;
static uint8_t g_data_send_nbtrials = 0;
static int8_t g_data_send_msg_type = -1;
#ifdef CONFIG_LINKWAN
static uint8_t g_freqband_num = 0;
#endif    

static TimerEvent_t TxNextPacketTimer;
volatile DeviceState_t g_lwan_device_state = DEVICE_STATE_INIT;
static DeviceStatus_t g_lwan_device_status = DEVICE_STATUS_IDLE;

bool g_lora_debug = false;
static LWanDevConfig_t *g_lwan_dev_config_p = NULL;
static LWanMacConfig_t *g_lwan_mac_config_p = NULL;
static LWanDevKeys_t *g_lwan_dev_keys_p = NULL;
static LWanProdctConfig_t *g_lwan_prodct_config_p = NULL;
static void start_dutycycle_timer(void);

#ifdef CONFIG_PROJECT_CAINIAO
static void notify_host()
{
    pin_wakeup_SetDriveMode(pin_wakeup_DM_STRONG);
    pin_wakeup_Write(1);
    CyDelay(5);
    pin_wakeup_Write(0);
}
#else
static void notify_host()
{
}  
#endif    

static bool send_frame(void)
{
    McpsReq_t mcpsReq;
    LoRaMacTxInfo_t txInfo;
    uint8_t send_msg_type;

    if (LoRaMacQueryTxPossible(tx_data.BuffSize, &txInfo) != LORAMAC_STATUS_OK) {
        return true;
    }

    if(g_lwan_mac_config_p->modes.linkcheck_mode == 2) {
        MlmeReq_t mlmeReq;
        mlmeReq.Type = MLME_LINK_CHECK;
        LoRaMacMlmeRequest(&mlmeReq);
    }
    
    send_msg_type = g_data_send_msg_type>=0?g_data_send_msg_type:g_lwan_mac_config_p->modes.confirmed_msg;
    if (send_msg_type == LORAWAN_UNCONFIRMED_MSG) {
        MibRequestConfirm_t mibReq;
        mibReq.Type = MIB_CHANNELS_NB_REP;
        mibReq.Param.ChannelNbRep = g_data_send_nbtrials?g_data_send_nbtrials:
                                                    g_lwan_mac_config_p->nbtrials.unconf + 1;
        LoRaMacMibSetRequestConfirm(&mibReq);
    
        mcpsReq.Type = MCPS_UNCONFIRMED;
        mcpsReq.Req.Unconfirmed.fPort = g_lwan_mac_config_p->port;
        mcpsReq.Req.Unconfirmed.fBuffer = tx_data.Buff;
        mcpsReq.Req.Unconfirmed.fBufferSize = tx_data.BuffSize;
        mcpsReq.Req.Unconfirmed.Datarate = g_lwan_mac_config_p->datarate;
    } else {
        mcpsReq.Type = MCPS_CONFIRMED;
        mcpsReq.Req.Confirmed.fPort = g_lwan_mac_config_p->port;
        mcpsReq.Req.Confirmed.fBuffer = tx_data.Buff;
        mcpsReq.Req.Confirmed.fBufferSize = tx_data.BuffSize;
        mcpsReq.Req.Confirmed.NbTrials = g_data_send_nbtrials?g_data_send_nbtrials:
                                                    g_lwan_mac_config_p->nbtrials.conf+1;
        mcpsReq.Req.Confirmed.Datarate = g_lwan_mac_config_p->datarate; 
    }

    g_data_send_nbtrials = 0;
    g_data_send_msg_type = -1;
    
    if (LoRaMacMcpsRequest(&mcpsReq) == LORAMAC_STATUS_OK) {
        return false;
    }

    return true;
}

static void prepare_tx_frame(void)
{
    if (g_lwan_mac_config_p->modes.report_mode == TX_ON_TIMER) {
        app_callbacks->LoraTxData(&tx_data);
    }
}

#ifdef CONFIG_LINKWAN
static uint8_t get_freqband_num(void)
{
    uint8_t num = 0;
    uint16_t mask = g_lwan_dev_config_p->freqband_mask;

    for (uint8_t i = 0; i < 16; i++) {
        if ((mask & (1 << i)) && i != 1) {
            num++;
        }
    }
    return num;
}
static uint8_t get_next_freqband(void)
{
    uint8_t freqband[16];
    uint8_t freqnum = 0;
    uint16_t mask = g_lwan_dev_config_p->freqband_mask;

    freqband[freqnum++] = 1; //1A2
    for (uint8_t i = 0; i < 16; i++) {
        if ((mask & (1 << i)) && i != 1) {
            freqband[freqnum++] = i;
        }
    }
    
    return freqband[randr(0,freqnum-1)];
}
#endif

static void reset_join_state(void)
{
    g_lwan_device_state = DEVICE_STATE_JOIN;
}
static void on_tx_next_packet_timer_event(void)
{
    MibRequestConfirm_t mib_req;
    LoRaMacStatus_t status;

    TimerStop(&TxNextPacketTimer);

    mib_req.Type = MIB_NETWORK_JOINED;
    status = LoRaMacMibGetRequestConfirm(&mib_req);

    if (status == LORAMAC_STATUS_OK) {
        if (mib_req.Param.IsNetworkJoined == true) {
            g_lwan_device_state = DEVICE_STATE_SEND;
        } else {
            rejoin_flag = true;
            g_lwan_device_state = DEVICE_STATE_JOIN;
        }
    }
}

static void mcps_confirm(McpsConfirm_t *mcpsConfirm)
{
    if (mcpsConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
#ifdef CONFIG_LINKWAN_AT   
        notify_host();
        PRINTF_AT("\r\nOK+SENT:%02X\r\n", mcpsConfirm->NbRetries);
#endif        
    } else {
#ifdef CONFIG_LINKWAN_AT   
        notify_host();
        PRINTF_AT("\r\nERR+SENT:%02X\r\n", mcpsConfirm->NbRetries);
#endif  

#if 0
        if(mcpsConfirm->McpsRequest ==MCPS_CONFIRMED) {
            if(g_lwan_dev_config_p->modes.join_mode == JOIN_MODE_OTAA) {
                reset_join_state();
#ifdef CONFIG_LINKWAN            
                MibRequestConfirm_t mibReq;
                uint16_t channelsMaskTemp[8] = {0};

                g_lwan_dev_config_p->join_settings.join_method = JOIN_METHOD_DEF;
                for (uint8_t i = 0; i < 16; i++) {
                    if ((g_lwan_dev_config_p->freqband_mask & (1 << i)) != 0) {
                        channelsMaskTemp[i / 2] |= (0xFF << ((i % 2) * 8));
                    }
                }
                channelsMaskTemp[0] |= 0XFF00;
                mibReq.Type = MIB_CHANNELS_MASK;
                mibReq.Param.ChannelsMask = channelsMaskTemp;
                LoRaMacMibSetRequestConfirm(&mibReq);
#endif    
                DBG_LINKWAN("Not receive Ack,Start to Join...\r\n");
            }else{
#ifdef CONFIG_LINKWAN
                MibRequestConfirm_t mibReq;
                mibReq.Type = MIB_FREQ_BAND;
                mibReq.Param.freqband = get_next_freqband();
                LoRaMacMibSetRequestConfirm(&mibReq);
#endif  
            }
        }
#endif         
    }
    next_tx = true;
}

static void mcps_indication(McpsIndication_t *mcpsIndication)
{
    if ( mcpsIndication->Status != LORAMAC_EVENT_INFO_STATUS_OK ) {
        return;
    }
    // Check Multicast
    // Check Port
    // Check Datarate
    // Check FramePending
    // Check Buffer
    // Check BufferSize
    // Check Rssi
    // Check Snr
    // Check RxSlot
    DBG_PRINTF( "receive data: rssi = %d, snr = %d, datarate = %d\r\n", mcpsIndication->Rssi, mcpsIndication->Snr,
                 mcpsIndication->RxDatarate);
    lwan_dev_status_set(DEVICE_STATUS_SEND_PASS_WITH_DL);
    if (mcpsIndication->RxData == true) {
        switch ( mcpsIndication->Port ) {
            case 224:
                break;
            default: {            
                rx_data.Port = mcpsIndication->Port;
                rx_data.BuffSize = mcpsIndication->BufferSize;
                memcpy1(rx_data.Buff, mcpsIndication->Buffer, rx_data.BuffSize);
                app_callbacks->LoraRxData(&rx_data);
                break;
            }
        }
#ifdef CONFIG_DEBUG_LINKWAN
    } else if (mcpsIndication->AckReceived) {
        DBG_LINKWAN( "rx, ACK, index %u\r\n", (unsigned int)g_ack_index++);
#endif
    }
#ifdef CONFIG_LINKWAN_AT   
    uint8_t confirm = 0;
    if(mcpsIndication->McpsIndication==MCPS_UNCONFIRMED)
        confirm = 0;
    else if(mcpsIndication->McpsIndication==MCPS_CONFIRMED)
        confirm = 1;
    uint8_t type = confirm | mcpsIndication->AckReceived<<1 | 
                   mcpsIndication->LinkCheckAnsReceived<<2 | mcpsIndication->DevTimeAnsReceived<<3;
    notify_host();
    PRINTF_AT("\r\nOK+RECV:%02X,%02X,%02X", type, mcpsIndication->Port, mcpsIndication->BufferSize);
    if(mcpsIndication->BufferSize) {
        PRINTF_AT(",");
        for(int i=0; i<mcpsIndication->BufferSize; i++) {
            PRINTF_AT("%02X", mcpsIndication->Buffer[i]);
        }
    }
    PRINTF_AT("\r\n");
#endif

#ifdef CONFIG_LWAN    
    if(mcpsIndication->UplinkNeeded) {
        g_lwan_device_state = DEVICE_STATE_SEND_MAC;
    }
#endif 
}

static uint32_t generate_rejoin_delay(void)
{
    uint32_t rejoin_delay = 0;

    while (rejoin_delay < g_lwan_dev_config_p->join_settings.join_interval*1000) {
        rejoin_delay += (rand1() % 250);
    }

    return rejoin_delay;
}

static void mlme_confirm( MlmeConfirm_t *mlmeConfirm )
{
    uint32_t rejoin_delay = 8*1000;
    MibRequestConfirm_t mibReq;

    switch ( mlmeConfirm->MlmeRequest ) {
        case MLME_JOIN: {
            if (mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
                // Status is OK, node has joined the network
                g_lwan_device_state = DEVICE_STATE_JOINED;
                lwan_dev_status_set(DEVICE_STATUS_JOIN_PASS);
#ifdef CONFIG_LINKWAN_AT
                notify_host();
                PRINTF_AT("%s:OK\r\n", LORA_AT_CJOIN);
#endif                
            } else {
                lwan_dev_status_set(DEVICE_STATUS_JOIN_FAIL);
                
#ifdef CONFIG_LINKWAN                
                // Join was not successful. Try to join again
                reset_join_state();
                if (g_lwan_dev_config_p->join_settings.join_method != JOIN_METHOD_SCAN) {
                    g_lwan_dev_config_p->join_settings.join_method = 
                        (g_lwan_dev_config_p->join_settings.join_method + 1) % JOIN_METHOD_NUM;
                    rejoin_delay = generate_rejoin_delay();
                    if (g_lwan_dev_config_p->join_settings.join_method == JOIN_METHOD_SCAN) {
                        g_freqband_num = get_freqband_num();
                    }
                }

                if (g_lwan_dev_config_p->join_settings.join_method == JOIN_METHOD_SCAN) {
                    if (g_freqband_num == 0) {
                        g_lwan_dev_config_p->join_settings.join_method = JOIN_METHOD_DEF;
                        rejoin_delay = 60 * 60 * 1000;  // 1 hour
#ifdef CONFIG_LINKWAN_AT      
                        notify_host();
                        PRINTF_AT("%s:FAIL\r\n", LORA_AT_CJOIN);
#endif                        
                        DBG_LINKWAN("Wait 1 hour for new round of scan\r\n");
                    } else {
                        g_freqband_num--;
                        rejoin_delay = generate_rejoin_delay();
                    }
                }
#else
#ifdef CONFIG_LINKWAN_AT                          
                PRINTF_AT("%s:FAIL\r\n", LORA_AT_CJOIN);
#endif          
                rejoin_delay = generate_rejoin_delay();
#endif    
                TimerSetValue(&TxNextPacketTimer, rejoin_delay);
                TimerStart(&TxNextPacketTimer);
                rejoin_flag = false;
            }
            break;
        }
        case MLME_LINK_CHECK: {
#ifdef CONFIG_LINKWAN_AT
            notify_host();
            PRINTF_AT("+CLINKCHECK: %d, %d, %d, %d, %d\r\n", mlmeConfirm->Status, mlmeConfirm->DemodMargin, mlmeConfirm->NbGateways, mlmeConfirm->Rssi, mlmeConfirm->Snr);
#endif            
            if ( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK ) {
                // Check DemodMargin
                // Check NbGateways
            } else {
                lwan_dev_status_set(DEVICE_STATUS_NETWORK_ABNORMAL);
            }
            break;
        }
        case MLME_DEVICE_TIME:
        {
            if( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK ){
                // Switch to the next state immediately
                g_lwan_device_state = DEVICE_STATE_BEACON_ACQUISITION;
                next_tx = true;
            } else {
                //No device time Ans
                g_lwan_device_state = DEVICE_STATE_SLEEP;
            }
            
            break;
        }
        case MLME_BEACON_ACQUISITION:
        {
            if( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK ) {
                //beacon received
                g_lwan_device_state = DEVICE_STATE_REQ_PINGSLOT_ACK;
                g_beacon_retry_times = 0;
            } else {
                //beacon lost
                if(g_beacon_retry_times < MAX_BEACON_RETRY_TIMES) {
                    g_beacon_retry_times ++;
                    g_lwan_device_state = DEVICE_STATE_REQ_DEVICE_TIME;
                } else {
                    g_beacon_retry_times = 0;
                    g_lwan_device_state = DEVICE_STATE_SLEEP;
                }
            }
            break;
        }
        case MLME_PING_SLOT_INFO:
        {
            if( mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK )
            {
                mibReq.Type = MIB_DEVICE_CLASS;
                mibReq.Param.Class = CLASS_B;
                LoRaMacMibSetRequestConfirm( &mibReq );
                
                mibReq.Type = MIB_PING_SLOT_DATARATE;
                mibReq.Param.PingSlotDatarate = g_lwan_dev_config_p->classb_param.pslot_dr;
                LoRaMacMibSetRequestConfirm( &mibReq );

                g_lwan_device_state = DEVICE_STATE_SEND;
                next_tx = true;
            }
            else
            {
                g_lwan_device_state = DEVICE_STATE_REQ_PINGSLOT_ACK;
            }
            break;
        }
        default:
            break;
    }
    next_tx = true;
}

static void mlme_indication( MlmeIndication_t *mlmeIndication )
{
    MibRequestConfirm_t mibReq;

    switch( mlmeIndication->MlmeIndication )
    {
        case MLME_SCHEDULE_UPLINK:
        {// The MAC signals that we shall provide an uplink as soon as possible
            g_lwan_device_state = DEVICE_STATE_SEND_MAC;
            break;
        }
        case MLME_BEACON_LOST:
        {
            mibReq.Type = MIB_DEVICE_CLASS;
            mibReq.Param.Class = CLASS_A;
            LoRaMacMibSetRequestConfirm( &mibReq );

            // Switch to class A again
            g_lwan_device_state = DEVICE_STATE_REQ_DEVICE_TIME;
            break;
        }
        case MLME_BEACON:
        {
            if( mlmeIndication->Status == LORAMAC_EVENT_INFO_STATUS_BEACON_LOCKED )
            {
                if(mlmeIndication->BeaconInfo.GwSpecific.InfoDesc==3){ //NetID+GatewayID
                    uint8_t *info = mlmeIndication->BeaconInfo.GwSpecific.Info;
                    if((gGatewayID[0]|gGatewayID[1]|gGatewayID[2]) 
                    && (memcmp(&info[3],gGatewayID,3)!=0)){//GatewayID not 0 and changed
                        //send an uplink in [0:120] seconds
                        TimerStop(&TxNextPacketTimer);
                        TimerSetValue(&TxNextPacketTimer,randr(0,120000));
                        TimerStart(&TxNextPacketTimer);                       
                    }
                    memcpy(gGatewayID,&info[3],3);
                }
            }
            break;
        }
        default:
            break;
    }
}


static void start_dutycycle_timer(void)
{
    MibRequestConfirm_t mib_req;
    LoRaMacStatus_t status;

    TimerStop(&TxNextPacketTimer);
    mib_req.Type = MIB_NETWORK_JOINED;
    status = LoRaMacMibGetRequestConfirm(&mib_req);
    if (status == LORAMAC_STATUS_OK) {
        if (mib_req.Param.IsNetworkJoined == true &&
            g_lwan_mac_config_p->modes.report_mode == TX_ON_TIMER && g_lwan_mac_config_p->report_interval != 0) {
            TimerSetValue(&TxNextPacketTimer, g_lwan_mac_config_p->report_interval*1000);
            TimerStart(&TxNextPacketTimer);
            return;
        }
    }
    if (g_lwan_mac_config_p->report_interval == 0 && g_lwan_mac_config_p->modes.report_mode == TX_ON_TIMER) {
        g_lwan_mac_config_p->modes.report_mode = TX_ON_NONE;
    }
}

MulticastParams_t *get_lora_cur_multicast(void)
{
    MibRequestConfirm_t mib_req;
    LoRaMacStatus_t status;

    mib_req.Type = MIB_MULTICAST_CHANNEL;
    status = LoRaMacMibGetRequestConfirm(&mib_req);
    if (status == LORAMAC_STATUS_OK) {
        return mib_req.Param.MulticastList;
    }
    return NULL;
}

static void print_dev_info(void)
{
    if(g_lwan_dev_config_p->modes.join_mode == JOIN_MODE_OTAA){
        DBG_LINKWAN("OTAA\r\n" );
        DBG_LINKWAN("DevEui= %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\r\n",
                    g_lwan_dev_keys_p->ota.deveui[0], g_lwan_dev_keys_p->ota.deveui[1], g_lwan_dev_keys_p->ota.deveui[2], g_lwan_dev_keys_p->ota.deveui[3], \
                    g_lwan_dev_keys_p->ota.deveui[4], g_lwan_dev_keys_p->ota.deveui[5], g_lwan_dev_keys_p->ota.deveui[6], g_lwan_dev_keys_p->ota.deveui[7]);
        DBG_LINKWAN("AppEui= %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\r\n",
                    g_lwan_dev_keys_p->ota.appeui[0], g_lwan_dev_keys_p->ota.appeui[1], g_lwan_dev_keys_p->ota.appeui[2], g_lwan_dev_keys_p->ota.appeui[3], \
                    g_lwan_dev_keys_p->ota.appeui[4], g_lwan_dev_keys_p->ota.appeui[5], g_lwan_dev_keys_p->ota.appeui[6], g_lwan_dev_keys_p->ota.appeui[7]);
        DBG_LINKWAN("AppKey= %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\r\n",
                    g_lwan_dev_keys_p->ota.appkey[0], g_lwan_dev_keys_p->ota.appkey[1], g_lwan_dev_keys_p->ota.appkey[2], g_lwan_dev_keys_p->ota.appkey[3], \
                    g_lwan_dev_keys_p->ota.appkey[4], g_lwan_dev_keys_p->ota.appkey[5], g_lwan_dev_keys_p->ota.appkey[6], g_lwan_dev_keys_p->ota.appkey[7], \
                    g_lwan_dev_keys_p->ota.appkey[8], g_lwan_dev_keys_p->ota.appkey[9], g_lwan_dev_keys_p->ota.appkey[10], g_lwan_dev_keys_p->ota.appkey[11], \
                    g_lwan_dev_keys_p->ota.appkey[12], g_lwan_dev_keys_p->ota.appkey[13], g_lwan_dev_keys_p->ota.appkey[14], g_lwan_dev_keys_p->ota.appkey[15]);
    } else if(g_lwan_dev_config_p->modes.join_mode == JOIN_MODE_ABP){
        DBG_LINKWAN("ABP\r\n");
        DBG_LINKWAN("DevAddr= %08X\r\n", (unsigned int)g_lwan_dev_keys_p->abp.devaddr);
        DBG_LINKWAN("NwkSKey= ");
        for (int i = 0; i < LORA_KEY_LENGTH; i++) {
            PRINTF_RAW("%02X", g_lwan_dev_keys_p->abp.nwkskey[i]);
        };
        PRINTF_RAW("\r\n");
        DBG_LINKWAN("AppSKey= ");
        for (int i = 0; i < LORA_KEY_LENGTH; i++) {
            PRINTF_RAW("%02X", g_lwan_dev_keys_p->abp.appskey[i]);
        };
        PRINTF_RAW("\r\n");
    }
    DBG_LINKWAN("class type %c\r\n", 'A' + g_lwan_dev_config_p->modes.class_mode);
    DBG_LINKWAN("freq mode %s\r\n", g_lwan_dev_config_p->modes.uldl_mode == ULDL_MODE_INTER ? "inter" : "intra");
    DBG_LINKWAN("scan chn mask 0x%04x\r\n", g_lwan_dev_config_p->freqband_mask);
}

void init_lwan_configs() 
{
    LWanDevKeys_t default_keys = LWAN_DEV_KEYS_DEFAULT;
    LWanDevConfig_t default_dev_config = LWAN_DEV_CONFIG_DEFAULT;
    LWanMacConfig_t default_mac_config = LWAN_MAC_CONFIG_DEFAULT;
    LWanProdctConfig_t default_prodct_config = LWAN_PRODCT_CONFIG_DEFAULT;
    g_lwan_dev_keys_p = lwan_dev_keys_init(&default_keys);
    g_lwan_dev_config_p = lwan_dev_config_init(&default_dev_config);
    g_lwan_mac_config_p = lwan_mac_config_init(&default_mac_config);
    g_lwan_prodct_config_p = lwan_prodct_config_init(&default_prodct_config);
}


void lora_init(LoRaMainCallback_t *callbacks)
{
    g_lwan_device_state = DEVICE_STATE_INIT;
    app_callbacks = callbacks;

#ifdef AOS_KV
    assert(aos_kv_init() == 0);
#endif

#ifdef CONFIG_LINKWAN_AT
    linkwan_at_init();
#endif
}

void lora_fsm( void )
{
    while (1) {
#ifdef CONFIG_LINKWAN_AT
        linkwan_at_process();
#endif
        if (Radio.IrqProcess != NULL) {
            Radio.IrqProcess();
        }
        
        switch (g_lwan_device_state) {
            case DEVICE_STATE_INIT: { 
                LoRaMacPrimitives.MacMcpsConfirm = mcps_confirm;
                LoRaMacPrimitives.MacMcpsIndication = mcps_indication;
                LoRaMacPrimitives.MacMlmeConfirm = mlme_confirm;
                LoRaMacPrimitives.MacMlmeIndication = mlme_indication;
                LoRaMacCallbacks.GetBatteryLevel = app_callbacks->BoardGetBatteryLevel;
                LoRaMacCallbacks.GetTemperatureLevel = NULL;
#if defined(REGION_AS923)
                LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_AS923);
#elif defined(REGION_AU915)
                LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_AU915);
#elif defined(REGION_CN470)
                LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_CN470);
#elif defined(REGION_CN779)
                LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_CN779);
#elif defined(REGION_EU433)
                LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_EU433);
#elif defined(REGION_IN865)
                LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_IN865);
#elif defined(REGION_EU868)
                LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_EU868);
#elif defined(REGION_KR920)
                LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_KR920);
#elif defined(REGION_US915)
                LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_US915);
#elif defined(REGION_US915_HYBRID)
                LoRaMacInitialization(&LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_US915_HYBRID);
#elif defined( REGION_CN470A )
                LoRaMacInitialization( &LoRaMacPrimitives, &LoRaMacCallbacks, LORAMAC_REGION_CN470A);
#else
#error "Please define a region in the compiler options."
#endif
                init_lwan_configs();
                if(!lwan_is_key_valid(g_lwan_dev_keys_p->pkey, LORA_KEY_LENGTH))
                    print_dev_info();
                
                TimerInit( &TxNextPacketTimer, on_tx_next_packet_timer_event );

                lwan_dev_params_update();
                lwan_mac_params_update();
                
                if(g_lwan_dev_config_p->modes.join_mode == JOIN_MODE_ABP){
                    MibRequestConfirm_t mibReq;
                    mibReq.Type = MIB_NET_ID;
                    mibReq.Param.NetID = LORAWAN_NETWORK_ID;
                    LoRaMacMibSetRequestConfirm(&mibReq);
                    mibReq.Type = MIB_DEV_ADDR;
                    mibReq.Param.DevAddr = g_lwan_dev_keys_p->abp.devaddr;
                    LoRaMacMibSetRequestConfirm(&mibReq);    
                    mibReq.Type = MIB_NWK_SKEY;
                    mibReq.Param.NwkSKey = g_lwan_dev_keys_p->abp.nwkskey;
                    LoRaMacMibSetRequestConfirm(&mibReq);
                    mibReq.Type = MIB_APP_SKEY;
                    mibReq.Param.AppSKey = g_lwan_dev_keys_p->abp.appskey;
                    LoRaMacMibSetRequestConfirm(&mibReq);
#ifdef CONFIG_LINKWAN                    
                    mibReq.Type = MIB_FREQ_BAND;
                    mibReq.Param.freqband = get_next_freqband();
                    LoRaMacMibSetRequestConfirm(&mibReq);
#endif                    
                    mibReq.Type = MIB_NETWORK_JOINED;
                    mibReq.Param.IsNetworkJoined = true;
                    LoRaMacMibSetRequestConfirm(&mibReq);
                    
                    lwan_mac_params_update();
#ifdef CONFIG_LORA_VERIFY 
                    g_lwan_device_state = DEVICE_STATE_SEND;
#else
                    g_lwan_device_state = DEVICE_STATE_SLEEP;
#endif    
		        }else if(g_lwan_dev_config_p->modes.join_mode == JOIN_MODE_OTAA) {
                    if(g_lwan_dev_config_p->join_settings.auto_join){
                        g_lwan_device_state = DEVICE_STATE_JOIN;
                    } else {
                        g_lwan_device_state = DEVICE_STATE_SLEEP;
                        linkwan_at_prompt_print();
                    }
                }
                lwan_dev_status_set(DEVICE_STATUS_IDLE);
                break;
            }

            case DEVICE_STATE_JOIN: {
                if(g_lwan_dev_config_p->modes.join_mode == JOIN_MODE_OTAA){
                    MlmeReq_t mlmeReq;

                    mlmeReq.Type = MLME_JOIN;
                    mlmeReq.Req.Join.DevEui = g_lwan_dev_keys_p->ota.deveui;
                    mlmeReq.Req.Join.AppEui = g_lwan_dev_keys_p->ota.appeui;
                    mlmeReq.Req.Join.AppKey = g_lwan_dev_keys_p->ota.appkey;
#ifdef CONFIG_LINKWAN    
                    mlmeReq.Req.Join.method = g_lwan_dev_config_p->join_settings.join_method;
                    if (g_lwan_dev_config_p->join_settings.join_method == JOIN_METHOD_STORED) {
                        mlmeReq.Req.Join.freqband = g_lwan_dev_config_p->join_settings.stored_freqband;
                        mlmeReq.Req.Join.datarate = g_lwan_dev_config_p->join_settings.stored_datarate;
                        mlmeReq.Req.Join.NbTrials = 3;
                    } else {
                        mlmeReq.Req.Join.NbTrials = g_lwan_dev_config_p->join_settings.join_trials;
                    }
#else
                    mlmeReq.Req.Join.NbTrials = g_lwan_dev_config_p->join_settings.join_trials;
#endif

                    if (next_tx == true && rejoin_flag == true) {
                        if (LoRaMacMlmeRequest(&mlmeReq) == LORAMAC_STATUS_OK) {
                            next_tx = false;
                        }
#ifdef CONFIG_LINKWAN                        
                        DBG_LINKWAN("Start to Join, method %d, nb_trials:%d\r\n",
                                    g_lwan_dev_config_p->join_settings.join_method, mlmeReq.Req.Join.NbTrials);
#else
                        DBG_LINKWAN("Start to Join, nb_trials:%d\r\n", mlmeReq.Req.Join.NbTrials);
#endif                        
    
                    }
		        }
                g_lwan_device_state = DEVICE_STATE_SLEEP;
                break;
            }
            case DEVICE_STATE_JOINED: {
                DBG_LINKWAN("Joined\n\r");
#ifdef CONFIG_LINKWAN                
                JoinSettings_t join_settings;
                lwan_dev_config_get(DEV_CONFIG_JOIN_SETTINGS, &join_settings);
                    
                MibRequestConfirm_t mib_req;
                mib_req.Type = MIB_FREQ_BAND;
                LoRaMacMibGetRequestConfirm(&mib_req);
                join_settings.stored_freqband = mib_req.Param.freqband;
                mib_req.Type = MIB_CHANNELS_DATARATE;
                LoRaMacMibGetRequestConfirm(&mib_req);
                join_settings.stored_datarate = mib_req.Param.ChannelsDatarate;
                join_settings.join_method = JOIN_METHOD_STORED;
                
                lwan_dev_config_set(DEV_CONFIG_JOIN_SETTINGS, &join_settings);
#endif                
                
                lwan_mac_params_update();
                
                if(g_lwan_dev_config_p->modes.class_mode == CLASS_B) {
                    g_lwan_device_state = DEVICE_STATE_REQ_DEVICE_TIME;
                }else{
#ifdef CONFIG_LORA_VERIFY                    
                    g_lwan_device_state = DEVICE_STATE_SEND;
#else
                    g_lwan_device_state = DEVICE_STATE_SLEEP;
#endif    
                }
                break;
            }
            case DEVICE_STATE_REQ_DEVICE_TIME: {
                MlmeReq_t mlmeReq;
                MibRequestConfirm_t mib_req;

                mib_req.Type = MIB_NETWORK_JOINED;
                LoRaMacMibGetRequestConfirm(&mib_req);
                if (mib_req.Param.IsNetworkJoined == true) {
                    if( next_tx == true ) {
                        mlmeReq.Type = MLME_DEVICE_TIME;
                        LoRaMacMlmeRequest( &mlmeReq );
                    }
                    g_lwan_device_state = DEVICE_STATE_SEND_MAC;
                } else {
                    g_lwan_device_state = DEVICE_STATE_SLEEP;
                }
                
                break;
            }
            case DEVICE_STATE_BEACON_ACQUISITION: {
                MlmeReq_t mlmeReq;

                if( next_tx == true ) {
                    if(g_lwan_dev_config_p->classb_param.beacon_freq)
                        LoRaMacClassBBeaconFreqReq(g_lwan_dev_config_p->classb_param.beacon_freq);
                    if(g_lwan_dev_config_p->classb_param.pslot_freq)
                        LoRaMacClassBPingSlotChannelReq(g_lwan_dev_config_p->classb_param.pslot_dr, g_lwan_dev_config_p->classb_param.pslot_freq);
                    mlmeReq.Type = MLME_BEACON_ACQUISITION;
                    LoRaMacMlmeRequest( &mlmeReq );
                }
                g_lwan_device_state = DEVICE_STATE_SLEEP;
                break;
            }
            case DEVICE_STATE_REQ_PINGSLOT_ACK: {
                MlmeReq_t mlmeReq;

                if( next_tx == true ) {
                    mlmeReq.Type = MLME_PING_SLOT_INFO;
                    mlmeReq.Req.PingSlotInfo.PingSlot.Fields.Periodicity = g_lwan_dev_config_p->classb_param.periodicity;
                    mlmeReq.Req.PingSlotInfo.PingSlot.Fields.RFU = 0;
                    LoRaMacMlmeRequest( &mlmeReq );
                }
                g_lwan_device_state = DEVICE_STATE_SEND_MAC;
                break;
            }
            case DEVICE_STATE_SEND: {
                if (next_tx == true) {
                    prepare_tx_frame();
                    next_tx = send_frame();
                }
                if (g_lwan_mac_config_p->modes.report_mode == TX_ON_TIMER) {
                    start_dutycycle_timer();
                }
                
                g_lwan_device_state = DEVICE_STATE_SLEEP;
                break;
            }
            case DEVICE_STATE_SEND_MAC: {
                if (next_tx == true) {
                    tx_data.BuffSize = 0;
                    next_tx = send_frame();
                }
                g_lwan_device_state = DEVICE_STATE_SLEEP;
                break;
            }
            case DEVICE_STATE_SLEEP: {       
#ifndef LOW_POWER_DISABLE
                LowPower_Handler( );
#endif
                break;
            }
            default: {
                g_lwan_device_state = DEVICE_STATE_INIT;
                break;
            }
        }
    }
}

DeviceState_t lwan_dev_state_get( void )
{
    return g_lwan_device_state;
}

void lwan_dev_state_set(DeviceState_t state)
{
    if (g_lwan_device_state == DEVICE_STATE_SLEEP) {
        TimerStop(&TxNextPacketTimer);
    }
    g_lwan_device_state = state;
}

bool lwan_dev_status_set(DeviceStatus_t ds)
{
    g_lwan_device_status = ds;
    return true;
}
DeviceStatus_t lwan_dev_status_get(void)
{
    return g_lwan_device_status;
}

bool lwan_is_dev_busy()
{
    MibRequestConfirm_t mibReq;
    mibReq.Type = MIB_MAC_STATE;
    LoRaMacMibGetRequestConfirm(&mibReq);
    
    if(g_lwan_device_state == DEVICE_STATE_SLEEP 
        && mibReq.Param.LoRaMacState == 0)
        return false;
    
    return true;
}

int lwan_mac_req_send(int type, void *param)
{
    MlmeReq_t mlmeReq;
    int ret = LWAN_SUCCESS;
    
    switch(type) {
        case MAC_REQ_LINKCHECK: {
            mlmeReq.Type = MLME_LINK_CHECK;
            break;
        }
        case MAC_REQ_DEVICE_TIME: {
            break;
        }
        case MAC_REQ_PSLOT_INFO: {
            uint8_t periodicity = *(uint8_t *)param;
            if(periodicity>7) 
                return LWAN_ERROR;
            
            mlmeReq.Type = MLME_PING_SLOT_INFO;
            mlmeReq.Req.PingSlotInfo.PingSlot.Fields.Periodicity = periodicity;
            mlmeReq.Req.PingSlotInfo.PingSlot.Fields.RFU = 0;
            
            ClassBParam_t classb_param;
            lwan_dev_config_get(DEV_CONFIG_CLASSB_PARAM, &classb_param);
            classb_param.periodicity = periodicity;
            lwan_dev_config_set(DEV_CONFIG_CLASSB_PARAM, &classb_param);
            break;
        }
        default: {
            ret = LWAN_ERROR;
            break;
        }
    }
    
    if (LoRaMacMlmeRequest(&mlmeReq) == LORAMAC_STATUS_OK) {
        g_lwan_device_state = DEVICE_STATE_SEND_MAC;
    }
    

    return ret;
}

int lwan_join(uint8_t bJoin, uint8_t bAutoJoin, uint16_t joinInterval, uint16_t joinRetryCnt)
{
    int ret = LWAN_SUCCESS;
    JoinSettings_t join_settings;
    lwan_dev_config_get(DEV_CONFIG_JOIN_SETTINGS, &join_settings);
    join_settings.auto_join = bAutoJoin;
    if(joinInterval>=7 && joinInterval<=255)
        join_settings.join_interval = joinInterval;
    if(joinRetryCnt>=1 && joinRetryCnt<=255)
        join_settings.join_trials = joinRetryCnt;
    lwan_dev_config_set(DEV_CONFIG_JOIN_SETTINGS, &join_settings);
        
    if(bJoin == 0){//stop join
        TimerStop(&TxNextPacketTimer);
        MibRequestConfirm_t mib_req;
        LoRaMacStatus_t status;
        mib_req.Type = MIB_NETWORK_JOINED;
        mib_req.Param.IsNetworkJoined = false;
        status = LoRaMacMibSetRequestConfirm(&mib_req);

        if (status != LORAMAC_STATUS_OK)
            return LWAN_ERROR;
        g_lwan_device_state = DEVICE_STATE_SLEEP;
        rejoin_flag = bAutoJoin;
    } else if(bJoin == 1){
        MibRequestConfirm_t mib_req;
        mib_req.Type = MIB_NETWORK_JOINED;
        LoRaMacStatus_t status = LoRaMacMibGetRequestConfirm(&mib_req);
        if (status != LORAMAC_STATUS_OK) 
            return LWAN_ERROR;
        
        if (mib_req.Param.IsNetworkJoined == true) {
            mib_req.Type = MIB_NETWORK_JOINED;
            mib_req.Param.IsNetworkJoined = false;
            status = LoRaMacMibSetRequestConfirm(&mib_req);
            if(status  != LORAMAC_STATUS_OK) {
                return LWAN_ERROR;
            }
            DBG_LINKWAN("Rejoin again...\r");
        }
        
        TimerStop(&TxNextPacketTimer);   
        rejoin_flag = true;
        reset_join_state();
    } else{
        ret = LWAN_ERROR;
    }
    
    return ret;
}

int lwan_data_send(uint8_t confirm, uint8_t Nbtrials, uint8_t *payload, uint8_t len)
{
    MibRequestConfirm_t mib_req;
    LoRaMacStatus_t status;

    TimerStop(&TxNextPacketTimer);

    mib_req.Type = MIB_NETWORK_JOINED;
    status = LoRaMacMibGetRequestConfirm(&mib_req);
    if (status == LORAMAC_STATUS_OK) {
        if (mib_req.Param.IsNetworkJoined == true) {
            g_data_send_msg_type = confirm;
            memcpy(tx_data.Buff, payload, len);
            tx_data.BuffSize = len;
            g_data_send_nbtrials = Nbtrials;
            g_lwan_device_state = DEVICE_STATE_SEND;
            return LWAN_SUCCESS;
        }
    }
    return LWAN_ERROR;
}

int lwan_data_recv(uint8_t *port, uint8_t **payload, uint8_t *size)
{
    if(!port || !payload || !size)
        return LWAN_ERROR;
    *port = rx_data.Port;
    *size = rx_data.BuffSize;
    *payload = rx_data.Buff;
    
    rx_data.BuffSize = 0;
    return LWAN_SUCCESS;
}

uint8_t lwan_dev_battery_get()
{
    return app_callbacks->BoardGetBatteryLevel();
}

int lwan_dev_rssi_get(uint8_t band, int16_t *channel_rssi)
{
    //CN470A Only
    uint8_t FreqBandStartChannelNum[16] = {0, 8, 16, 24, 100, 108, 116, 124, 68, 76, 84, 92, 166, 174, 182, 190};
    if(band>=16) 
        return LWAN_ERROR;

    Radio.SetModem(MODEM_LORA);
    for (uint8_t i = 0; i < 8; i++) {
        uint32_t freq = 470300000 + (FreqBandStartChannelNum[band] + i) * 200000;
        
        Radio.SetChannel(freq);
        Radio.Rx( 0 );
        CyDelay(3);
        
        channel_rssi[i] = Radio.Rssi(MODEM_LORA);
    }
    
    Radio.Sleep();
    
    return LWAN_SUCCESS;
}


bool lwan_multicast_add(void *multicastInfo )
{
    MibRequestConfirm_t mibset;
    LoRaMacStatus_t status;
    MulticastParams_t *pmulticastInfo;

    pmulticastInfo = aos_malloc(sizeof(MulticastParams_t));
    if (!pmulticastInfo)
        return false;
    memcpy(pmulticastInfo, multicastInfo, sizeof(MulticastParams_t));
    mibset.Type = MIB_MULTICAST_CHANNEL;
    mibset.Param.MulticastList = pmulticastInfo;
    status = LoRaMacMibSetRequestConfirm(&mibset);
    if (status !=  LORAMAC_STATUS_OK) {
        return false;
    }
    return true;
}

bool lwan_multicast_del(uint32_t dev_addr)
{
    MulticastParams_t *multiCastNode = get_lora_cur_multicast();
    if (multiCastNode == NULL) {
        return false;
    }
    
    while (multiCastNode != NULL) {
        if (dev_addr == multiCastNode->Address) {
            MibRequestConfirm_t mibset;
            LoRaMacStatus_t status;
            mibset.Type = MIB_MULTICAST_CHANNEL_DEL;
            mibset.Param.MulticastList = multiCastNode;
            status = LoRaMacMibSetRequestConfirm(&mibset);
            if (status !=  LORAMAC_STATUS_OK) {
                return false;
            } else {
                aos_free(mibset.Param.MulticastList);
                return true;
            }
        }
        multiCastNode = multiCastNode->Next;

    }
    return false;
}

uint8_t lwan_multicast_num_get(void)
{
    MulticastParams_t *multiCastNode = get_lora_cur_multicast();
    if (multiCastNode == NULL) {
        return 0;
    }
    uint8_t num = 0;
    while (multiCastNode != NULL) {
        num++;
        multiCastNode = multiCastNode->Next;
    }
    return num;
}

void lwan_sys_reboot(int8_t mode)
{
    if (mode == 0) {
	    HW_Reset(0);
    } else if (mode == 1) {
        if (next_tx == true) {
            prepare_tx_frame();
            next_tx = send_frame();
            HW_Reset(0);
        }
    } else if (mode == 7) {
        HW_Reset(1);
    }
}


