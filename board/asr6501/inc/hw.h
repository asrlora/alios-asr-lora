#ifndef __HW_H__
#define __HW_H__

#include "debug.h"
#include "hw_conf.h"

    
typedef enum
{
  e_LOW_POWER_RTC = (1 << 0),
  e_LOW_POWER_GPS = (1 << 1),
  e_LOW_POWER_UART = (1 << 2), /* can be used to forbid stop mode in case of uart Xfer*/
} e_LOW_POWER_State_Id_t;

#ifdef __cplusplus
extern "C" {
#endif
uint8_t HW_GetBatteryLevel( void );
void HW_GetUniqueId( uint8_t *id );
uint32_t HW_GetRandomSeed( void );
void BoardDriverInit(void);
#ifdef __cplusplus
}
#endif
#endif
