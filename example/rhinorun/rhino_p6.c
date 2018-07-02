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
#include "project.h"
#include <k_api.h>
#include <stdio.h>
#include "hal/soc/soc.h"

#define DEMO_TASK_STACKSIZE    512 //512*cpu_stack_t = 2048byte
#define DEMO_TASK_PRIORITY     20

static ktask_t demo_task_obj;
cpu_stack_t demo_task_buf[DEMO_TASK_STACKSIZE];
 
int default_UART_Init(void);
void SerialDownload(void);

uart_dev_t uart_1;

int UART_Init(void)
{
    uart_1.port                = 1;
    uart_1.config.baud_rate    = 115200;
    uart_1.config.data_width   = DATA_WIDTH_8BIT;
    uart_1.config.parity       = NO_PARITY;
    uart_1.config.stop_bits    = STOP_BITS_1;
    uart_1.config.flow_control = FLOW_CONTROL_DISABLED;

    return hal_uart_init(&uart_1);
}

void demo_task(void *arg)
{
    (void) arg;
    int count = 0;
    
    default_UART_Init();
    
    printf("demo_task here!\n");
    
    UART_Init();
    
    SerialDownload();
    
    while (1)
    {
        printf("hello world! count %d\n", count++);

        //sleep 1 second
        krhino_task_sleep(RHINO_CONFIG_TICKS_PER_SECOND);
    };
}

void SysTick_IRQ(void)
{
    krhino_intrpt_enter();
    krhino_tick_proc();
    krhino_intrpt_exit();	
}

int application_start(int argc, char **argv)
{
		return 0;
}

int main(void)
{
    __enable_irq(); /* Enable global interrupts. */

    /* Place your initialization/startup code here (e.g. MyInst_Start()) */
    krhino_init();
    krhino_task_create(&demo_task_obj, "demo_task", 0,DEMO_TASK_PRIORITY, 
        50, demo_task_buf, DEMO_TASK_STACKSIZE, demo_task, 1);

    Cy_SysTick_Init(CY_SYSTICK_CLOCK_SOURCE_CLK_CPU, CYDEV_CLK_HFCLK0__HZ/RHINO_CONFIG_TICKS_PER_SECOND);
    Cy_SysTick_SetCallback(0, SysTick_IRQ);
    
    krhino_start();
    
    for(;;)
    {
        /* Place your application code here. */
    }
}

/* [] END OF FILE */
