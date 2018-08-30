/*
 * Copyright (C) 2015-2017 Alibaba Group Holding Limited
 */


#include "project.h"
#include <k_api.h>
#include <stdio.h>
#include <aos\aos.h>
#include <uart_port.h>
#include "hw_conf.h"
#include "spi.h"
#include "rtc-board.h"
#include "asr_timer.h"

#ifdef CERTIFICATION
#define AOS_START_STACK 256
#else
#define AOS_START_STACK 512
#endif
uint32_t setb_pin;
ktask_t *g_aos_init;
aos_task_t task;
static kinit_t kinit;
extern int application_start(int argc, char **argv);

void board_init(void);
#ifdef CERTIFICATION
    char *ptr;
    extern void test_certificate(void);
    extern void log_no_cli_init(void);
#endif
void PendSV_Handler(void);
#ifdef AOS_KV
int aos_kv_init(void);
#endif
#ifdef VCALL_RHINO
extern void dumpsys_cli_init(void);   
#endif
static void var_init()
{
    kinit.argc = 0;
    kinit.argv = NULL;
    kinit.cli_enable = 1;
}

void SysTick_IRQ(void)
{
    krhino_intrpt_enter();
    krhino_tick_proc();
    krhino_intrpt_exit();	
}

static void sys_init(void)
{
    default_UART_Init();
    board_init();
    var_init();
#ifdef AOS_KV
    aos_kv_init();
#endif
#ifndef CERTIFICATION
#ifdef CONFIG_LINKWAN_AT
#else
    aos_cli_init();
#endif
#ifdef VCALL_RHINO
    dumpsys_cli_init();
#endif
    application_start(kinit.argc,kinit.argv);
#else
    
    log_no_cli_init();
    test_certificate();
#endif

    while(1)
    {
        krhino_task_sleep(RHINO_CONFIG_TICKS_PER_SECOND*10);
    }
}

extern uint32_t g_rtc_period;
static void sys_start(void)
{
    kstat_t stat;    
    aos_init();
    setb_pin = set_b_PC;

    SpiInit();
     /* Configure SysTick timer to generate interrupt every 1 ms */
    CySysTickStart();
    CySysTickSetReload(CYDEV_BCLK__SYSCLK__HZ/RHINO_CONFIG_TICKS_PER_SECOND);
    CyIntSetSysVector(CY_INT_PEND_SV_IRQN, PendSV_Handler);
    CySysTickEnable();
    CySysTickSetCallback(0, SysTick_IRQ);
    /* set wco */
    Asr_Timer_Init();
    CySysTimerDisable(CY_SYS_TIMER2_MASK);
    CySysTimerResetCounters(CY_SYS_TIMER2_RESET);
    CySysTimerSetToggleBit(3);//0~31
    CySysTimerSetInterruptCallback(2, RTC_Update_ASR);
    CySysTimerEnableIsr(2);
    CySysTimerSetMode(2, CY_SYS_TIMER_MODE_INT);
    CySysTimerEnable(CY_SYS_TIMER2_MASK);
    RTC_Start();
    g_rtc_period = (32768 / (1 << CySysTimerGetToggleBit()));
    RTC_SetPeriod(1u, g_rtc_period);

    stat = krhino_task_dyn_create(&g_aos_init, "aos-init", 0, AOS_DEFAULT_APP_PRI, 0, AOS_START_STACK, (task_entry_t)sys_init, 1);
    if(stat != RHINO_SUCCESS)
    {
        return;
    }
       
    aos_start();
}

int main(void)
{
    CyGlobalIntEnable; /* Enable global interrupts. */
	  
    sys_start();
    return 0;
}

