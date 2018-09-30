/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __DEBUG_H__
#define __DEBUG_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include <stdio.h>
#include "hw_conf.h"
#include "timeServer.h"
#include "utilities.h"

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
/* External variables --------------------------------------------------------*/
/* Exported macros -----------------------------------------------------------*/
#ifdef CONFIG_DEBUG_LINKWAN
extern TimerTime_t timerContext;
    #define PRINTF printf
    #define DBG_PRINTF(format, ...)    do {TimerGetCurrentTime();printf("[%llu]", timerContext); printf(format, ##__VA_ARGS__);}while(0)
    #define PRINTF_RAW printf
    #define DBG_PRINTF_CRITICAL(p)
#endif


/* Exported functions ------------------------------------------------------- */

/**
 * @brief  Initializes the debug
 * @param  None
 * @retval None
 */
void DBG_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEBUG_H__*/

