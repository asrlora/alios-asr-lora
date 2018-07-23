/* ========================================
 *
 * Copyright YOUR COMPANY, THE YEAR
 * All Rights Reserved
 * UNPUBLISHED, LICENSED SOFTWARE.
 *
 * CONFIDENTIAL AND PROPRIETARY INFORMATION
 * WHICH IS THE PROPERTY OF your company.
 *
 * ========================================
*/
#ifndef _ASR_BOARD_TEST_H
#define _ASR_BOARD_TEST_H
#include <stdbool.h>
#include <stdio.h>
bool LoRaTestSleep(uint8_t sleep_mode);
bool LoRaTestMcu(uint8_t mcu_mode);
bool LoRaTestRxc(uint8_t dr);
bool LoRaTestTxc(uint8_t pwr);
bool LoRaTestStdby(uint8_t stdby);
typedef enum
{
    LOWPOWER,
    RX,
    RX_TIMEOUT,
    RX_ERROR,
    TX,
    TX_TIMEOUT,
}States_t;
#endif
/* [] END OF FILE */
