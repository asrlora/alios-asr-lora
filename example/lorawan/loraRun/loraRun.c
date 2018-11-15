/*
 * Copyright (C) 2015-2017 Alibaba Group Holding Limited
 */

#include "hw.h"
#include "low_power.h"
#include "linkwan_ica_at.h"
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

static void lora_task_entry(void *arg)
{
    BoardInitMcu();
    lora_init(&LoRaMainCallbacks);
    linkwan_at_custom_handler_set(loratest_at_process);   
    lora_fsm( );
}

int application_start( void )
{
    lora_task_entry(NULL);
    return 0;
}

static void LoraTxData( lora_AppData_t *AppData)
{
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
