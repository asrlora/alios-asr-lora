/*
 * Copyright (C) 2015-2017 Alibaba Group Holding Limited
 */

#include "hw.h"
#include "low_power.h"
#include "linkwan.h"
#include "timeServer.h"
//#include "version.h"
#include "radio.h"
//#include "sx1276Regs-Fsk.h"
//#include "sx1276Regs-LoRa.h"
#include "board_test.h"
#include "delay.h"
#include "board.h"

#include "LoRaMac.h"
#include "Region.h"
#include "commissioning.h"

#include <k_api.h>

#define APP_TX_DUTYCYCLE 30000
#define LORAWAN_ADR_ON 1
#define LORAWAN_CONFIRMED_MSG ENABLE
#define LORAWAN_APP_PORT 100
#define JOINREQ_NBTRIALS 3

static void LoraTxData(lora_AppData_t *AppData);
static void LoraRxData(lora_AppData_t *AppData);
uint8_t BoardGetBatteryLevel()
{
    return 100;
}
void BoardGetUniqueId(uint8_t *id)
{
}
uint32_t BoardGetRandomSeed()
{
    return 0;
}
static LoRaMainCallback_t LoRaMainCallbacks = {
    BoardGetBatteryLevel,
    BoardGetUniqueId,
    BoardGetRandomSeed,
    LoraTxData,
    LoraRxData
};

static LoRaParam_t LoRaParamInit = {
    TX_ON_TIMER,
    APP_TX_DUTYCYCLE,
    LORAWAN_ADR_ON,
    DR_0,
    LORAWAN_PUBLIC_NETWORK,
    JOINREQ_NBTRIALS
};

static void lora_task_entry(void *arg)
{
    BoardInitMcu();
    lora_init(&LoRaMainCallbacks);
    linkwan_at_custom_handler_set(process_loratest_at);
    lora_fsm( );
}

//ktask_t g_lora_task;
//cpu_stack_t g_lora_task_stack[512];
int application_start( void )
{
//    krhino_task_create(&g_lora_task, "lora_task", NULL,
//                       5, 0u, g_lora_task_stack,
//                       512, lora_task_entry, 1u);
    lora_task_entry(NULL);
}

static void LoraTxData( lora_AppData_t *AppData)
{
    extern uint32 tx_timer_cnt;
    AppData->BuffSize = sprintf( (char *) AppData->Buff, "linklora asr data++");
    PRINTF_RAW("tx: %s\r\n", AppData->Buff );
    AppData->Port = LORAWAN_APP_PORT;
}

static void LoraRxData( lora_AppData_t *AppData )
{
    AppData->Buff[AppData->BuffSize] = '\0';
    PRINTF_RAW( "rx: port = %d, len = %d\r\n", AppData->Port, AppData->BuffSize);
    int i;
    for (i = 0; i < AppData->BuffSize; i++) {
        PRINTF_RAW("0x%x ", AppData->Buff[i] );
    }
    PRINTF_RAW("\r\n");
}
