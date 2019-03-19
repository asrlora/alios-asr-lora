#include "project.h"
#include <stdio.h>
#ifdef AOS_KV
#include "kvmgr.h"
#endif
#include <uart_port.h>
#include "hw.h"
#include "hw_conf.h"
#include "spi.h"
#include "rtc-board.h"
#include "asr_timer.h"

extern int application_start();
extern void GpioIsrEntry (void);
extern void board_init(void);
static void sys_start(void)
{
    //board_init();
    SpiInit();
    default_UART_Init();
   
    global_irq_StartEx(GpioIsrEntry);
    /* set wco */
    Asr_Timer_Init();
    RtcInit();

    application_start();
}

int main(void)
{
    CyGlobalIntEnable; /* Enable global interrupts. */
	  
    sys_start();
    
    while(1){
    }
    return 0;
}