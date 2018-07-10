/*!
 * \file      sx1261dvk1bas-board.c
 *
 * \brief     Target board SX1261DVK1BAS shield driver implementation
 *
 * \copyright Revised BSD License, see section \ref LICENSE.
 *
 * \code
 *                ______                              _
 *               / _____)             _              | |
 *              ( (____  _____ ____ _| |_ _____  ____| |__
 *               \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 *               _____) ) ____| | | || |_| ____( (___| | | |
 *              (______/|_____)_|_|_| \__)_____)\____)_| |_|
 *              (C)2013-2017 Semtech
 *
 * \endcode
 *
 * \author    Miguel Luis ( Semtech )
 *
 * \author    Gregory Cristian ( Semtech )
 */
#include <project.h>
//#include "asr_project.h"
#include <stdlib.h>
#include "utilities.h"
#include "board-config.h"
#include "board.h"
#include "delay.h"
#include "radio.h"
#include "timer.h"
#include "sx126x-board.h"
#include "debug.h"
extern uint32_t sys_timeout;
extern uint32_t current_times;
/*!
 * Antenna switch GPIO pins objects
 */
Gpio_t AntPow;
Gpio_t DeviceSel;

void SX126xIoInit( void )
{
    GpioInit( &SX126x.Spi.Nss, RADIO_NSS, PIN_OUTPUT, PIN_PUSH_PULL, PIN_PULL_UP, 1 );
    GpioInit( &SX126x.BUSY, RADIO_BUSY, PIN_INPUT, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
    GpioInit( &SX126x.DIO1, RADIO_DIO_1, PIN_INPUT, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
    GpioInit( &DeviceSel, RADIO_DEVICE_SEL, PIN_INPUT, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
}

void SX126xIoIrqInit( DioIrqHandler dioIrq )
{
    GpioSetInterrupt( &SX126x.DIO1, IRQ_RISING_EDGE, IRQ_HIGH_PRIORITY, dioIrq );
}

void SX126xIoDeInit( void )
{
    GpioInit( &SX126x.Spi.Nss, RADIO_NSS, PIN_ANALOGIC, PIN_PUSH_PULL, PIN_PULL_UP, 1 );
    GpioInit( &SX126x.BUSY, RADIO_BUSY, PIN_ANALOGIC, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
    GpioInit( &SX126x.DIO1, RADIO_DIO_1, PIN_ANALOGIC, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
}

uint32_t SX126xGetBoardTcxoWakeupTime( void )
{
    return BOARD_TCXO_WAKEUP_TIME;
}

void SX126xReset( void )
{
    DelayMs( 10 );
    GpioInit( &SX126x.Reset, RADIO_RESET, PIN_OUTPUT, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
    DelayMs( 20 );
    GpioInit( &SX126x.Reset, RADIO_RESET, PIN_ANALOGIC, PIN_PUSH_PULL, PIN_NO_PULL, 1 ); // internal pull-up
    DelayMs( 10 );
}

void SX126xWaitOnBusy( void )
{
    while( GpioRead( &SX126x.BUSY ) == 1 );
}

void SX126xWakeup( void )
{
    BoardDisableIrq( );

    GpioWrite( &SX126x.Spi.Nss, 0 );
    SpiInOut( &SX126x.Spi, RADIO_GET_STATUS );
    SpiInOut( &SX126x.Spi, 0x00 );

    GpioWrite( &SX126x.Spi.Nss, 1 );

    // Wait for chip to be ready.
    SX126xWaitOnBusy( );
    BoardEnableIrq( );
}
#ifdef DEBUG_SX126X
static char* dumpCmd(RadioCommands_t command)
{
    switch(command){
        case RADIO_GET_STATUS: 
                return "RADIO_GET_STATUS";
        case RADIO_WRITE_REGISTER:
                return  "RADIO_WRITE_REGISTER";
        case RADIO_READ_REGISTER:
                return  "RADIO_READ_REGISTER";
        case RADIO_WRITE_BUFFER:
                return  "RADIO_WRITE_BUFFER";
        case RADIO_READ_BUFFER:
                return  "RADIO_READ_BUFFER";
        case RADIO_SET_SLEEP:
                return  "RADIO_SET_SLEEP";
        case RADIO_SET_STANDBY:
                return  "RADIO_SET_STANDBY";
        case RADIO_SET_FS:
                return  "RADIO_SET_FS";
        case RADIO_SET_TX:
                return  "RADIO_SET_TX";
        case RADIO_SET_RX:
                return  "RADIO_SET_RX";
        case RADIO_SET_RXDUTYCYCLE:
                return  "RADIO_SET_RXDUTYCYCLE";
        case RADIO_SET_CAD:
                return  "RADIO_SET_CAD";
        case RADIO_SET_TXCONTINUOUSWAVE:
                return  "RADIO_SET_TXCONTINUOUSWAVE";
        case RADIO_SET_TXCONTINUOUSPREAMBLE:
                return  "RADIO_SET_TXCONTINUOUSPREAMBLE";
        case RADIO_SET_PACKETTYPE:
                return  "RADIO_SET_PACKETTYPE";
        case RADIO_GET_PACKETTYPE:
                return  "RADIO_GET_PACKETTYPE";
        case RADIO_SET_RFFREQUENCY:
                return  "RADIO_SET_RFFREQUENCY";
        case RADIO_SET_TXPARAMS:
                return "RADIO_SET_TXPARAMS";
        case RADIO_SET_PACONFIG:
                return  "RADIO_SET_PACONFIG"; 
        case RADIO_SET_CADPARAMS:
                return  "RADIO_SET_CADPARAMS";
        case RADIO_SET_BUFFERBASEADDRESS:
                return  "RADIO_SET_BUFFERBASEADDRESS";
        case RADIO_SET_MODULATIONPARAMS:
                return  "RADIO_SET_MODULATIONPARAMS";
        case RADIO_SET_PACKETPARAMS:
                return  "RADIO_SET_PACKETPARAMS";
        case RADIO_GET_RXBUFFERSTATUS:
                return  "RADIO_GET_RXBUFFERSTATUS";
        case RADIO_GET_PACKETSTATUS:
                return  "RADIO_GET_PACKETSTATUS";
        case RADIO_GET_RSSIINST:
                return  "RADIO_GET_RSSIINST";
        case RADIO_GET_STATS:
                return  "RADIO_GET_STATS";
        case RADIO_RESET_STATS:
                return  "RADIO_RESET_STATS";
        case RADIO_CFG_DIOIRQ:
                return  "RADIO_CFG_DIOIRQ";
        case RADIO_GET_IRQSTATUS:
                return  "RADIO_GET_IRQSTATUS";
        case RADIO_CLR_IRQSTATUS:
                return  "RADIO_CLR_IRQSTATUS";
        case RADIO_CALIBRATE:
                return  "RADIO_CALIBRATE";
        case RADIO_CALIBRATEIMAGE:
                return  "RADIO_CALIBRATEIMAGE";
        case RADIO_SET_REGULATORMODE:
                return  "RADIO_SET_REGULATORMODE";
        case RADIO_GET_ERROR:
                return  "RADIO_GET_ERROR";
        case RADIO_CLR_ERROR:
                return  "RADIO_CLR_ERROR";
        case RADIO_SET_TCXOMODE:
                return  "RADIO_SET_TCXOMODE";
        case RADIO_SET_TXFALLBACKMODE:
                return  "RADIO_SET_TXFALLBACKMODE";
        case RADIO_SET_RFSWITCHMODE:
                return  "RADIO_SET_RFSWITCHMODE";
        case RADIO_SET_STOPRXTIMERONPREAMBLE:
                return  "RADIO_SET_STOPRXTIMERONPREAMBLE";
        case RADIO_SET_LORASYMBTIMEOUT:
                return  "RADIO_SET_LORASYMBTIMEOUT";
        default:
            printf("command 0x%x\r\n",command);
                return "INVALID!";
    }
}
#endif
void SX126xWriteCommand( RadioCommands_t command, uint8_t *buffer, uint16_t size )
{
#ifdef DEBUG_SX126X
    uint16_t i=0;
    printf("SX126xWriteCommand: %s ",dumpCmd(command));
    for(i=0;i<size;i++)
        printf("0x%x ",buffer[i]);
    printf("\r\n");
#endif
    SX126xCheckDeviceReady( );
    GpioWrite( &SX126x.Spi.Nss, 0 );

    SpiInOut( &SX126x.Spi, ( uint8_t )command );

    for( uint16_t i = 0; i < size; i++ )
    {
        SpiInOut( &SX126x.Spi, buffer[i] );
    }

    GpioWrite( &SX126x.Spi.Nss, 1 );

    if( command != RADIO_SET_SLEEP )
    {
        SX126xWaitOnBusy( );
    }
}

void SX126xReadCommand( RadioCommands_t command, uint8_t *buffer, uint16_t size )
{

    SX126xCheckDeviceReady( );
    GpioWrite( &SX126x.Spi.Nss, 0 );

    SpiInOut( &SX126x.Spi, ( uint8_t )command );
    SpiInOut( &SX126x.Spi, 0x00 );
    for( uint16_t i = 0; i < size; i++ )
    {
        buffer[i] = SpiInOut( &SX126x.Spi, 0 );
    }

    GpioWrite( &SX126x.Spi.Nss, 1 );
#ifdef DEBUG_SX126X
    uint16_t i=0;
    printf("SX126xReadCommand: %s ",dumpCmd(command));
    for(i=0;i<size;i++)
        printf("0x%x ",buffer[i]);
    printf("\r\n");
#endif
    SX126xWaitOnBusy( );
}

void SX126xWriteRegisters( uint16_t address, uint8_t *buffer, uint16_t size )
{
    #ifdef DEBUG_SX126X
    uint16_t i=0;
    printf("SX126xWriteReg: 0x%x ",address);
    for(i=0;i<size;i++)
        printf("0x%x ",buffer[i]);
    printf("\r\n");
    #endif
    SX126xCheckDeviceReady( );
    GpioWrite( &SX126x.Spi.Nss, 0 );

    SpiInOut( &SX126x.Spi, RADIO_WRITE_REGISTER );
    SpiInOut( &SX126x.Spi, ( address & 0xFF00 ) >> 8 );
    SpiInOut( &SX126x.Spi, address & 0x00FF );
    
    for( uint16_t i = 0; i < size; i++ )
    {
        SpiInOut( &SX126x.Spi, buffer[i] );
    }

    GpioWrite( &SX126x.Spi.Nss, 1 );

    SX126xWaitOnBusy( );
}

void SX126xWriteRegister( uint16_t address, uint8_t value )
{
    SX126xWriteRegisters( address, &value, 1 );
}

void SX126xReadRegisters( uint16_t address, uint8_t *buffer, uint16_t size )
{
    SX126xCheckDeviceReady( );

    GpioWrite( &SX126x.Spi.Nss, 0 );

    SpiInOut( &SX126x.Spi, RADIO_READ_REGISTER );
    SpiInOut( &SX126x.Spi, ( address & 0xFF00 ) >> 8 );
    SpiInOut( &SX126x.Spi, address & 0x00FF );
    SpiInOut( &SX126x.Spi, 0 );
    for( uint16_t i = 0; i < size; i++ )
    {
        buffer[i] = SpiInOut( &SX126x.Spi, 0 );
    }
    GpioWrite( &SX126x.Spi.Nss, 1 );

    SX126xWaitOnBusy( );
    #ifdef DEBUG_SX126X
    uint16_t i=0;
    printf("SX126xReadReg: 0x%x ",address);
    for(i=0;i<size;i++)
        printf("0x%x ",buffer[i]);
    printf("\r\n");
    #endif
}

uint8_t SX126xReadRegister( uint16_t address )
{
    uint8_t data;
    SX126xReadRegisters( address, &data, 1 );
    return data;
}

void SX126xWriteBuffer( uint8_t offset, uint8_t *buffer, uint8_t size )
{
    #ifdef DEBUG_SX126X
    uint16_t i=0;
    printf("SX126xWriteBuf: 0x%x ",offset);
    for(i=0;i<size;i++)
        printf("0x%x ",buffer[i]);
    printf("\r\n");
    #endif
    SX126xCheckDeviceReady( );

    GpioWrite( &SX126x.Spi.Nss, 0 );

    SpiInOut( &SX126x.Spi, RADIO_WRITE_BUFFER );
    SpiInOut( &SX126x.Spi, offset );
    for( uint16_t i = 0; i < size; i++ )
    {
        SpiInOut( &SX126x.Spi, buffer[i] );
    }
    GpioWrite( &SX126x.Spi.Nss, 1 );

    SX126xWaitOnBusy( );
}

void SX126xReadBuffer( uint8_t offset, uint8_t *buffer, uint8_t size )
{
    SX126xCheckDeviceReady( );

    GpioWrite( &SX126x.Spi.Nss, 0 );

    SpiInOut( &SX126x.Spi, RADIO_READ_BUFFER );
    SpiInOut( &SX126x.Spi, offset );
    SpiInOut( &SX126x.Spi, 0 );
    for( uint16_t i = 0; i < size; i++ )
    {
        buffer[i] = SpiInOut( &SX126x.Spi, 0 );
    }
    GpioWrite( &SX126x.Spi.Nss, 1 );

    SX126xWaitOnBusy( );
    #ifdef DEBUG_SX126X
    uint16_t i=0;
    printf("SX126xReadBuf: 0x%x ",offset);
    for(i=0;i<size;i++)
        printf("0x%x ",buffer[i]);
    printf("\r\n");
    #endif
}

void SX126xSetRfTxPower( int8_t power )
{
    SX126xSetTxParams( power, RADIO_RAMP_200_US );
}

uint8_t SX126xGetPaSelect( uint32_t channel )
{
    return SX1262;

    if( GpioRead( &DeviceSel ) == 1 )
    {
        return SX1261;
    }
    else
    {
        return SX1262;
    }
}

void SX126xAntSwOn( void )
{
    GpioInit( &AntPow, RADIO_ANT_SWITCH_POWER, PIN_OUTPUT, PIN_PUSH_PULL, PIN_PULL_UP, 1 );
}

void SX126xAntSwOff( void )
{
    GpioInit( &AntPow, RADIO_ANT_SWITCH_POWER, PIN_ANALOGIC, PIN_PUSH_PULL, PIN_NO_PULL, 0 );
}

bool SX126xCheckRfFrequency( uint32_t frequency )
{
    // Implement check. Currently all frequencies are supported
    return true;
}

void BoardDisableIrq( void )
{
    CyGlobalIntDisable;    
}

void BoardEnableIrq( void )
{
    CyGlobalIntEnable;
}

void RtcInit( void )
{
	RTC_Start();
    RTC_SetPeriod(200, 40000);
}

TimerTime_t RtcGetAdjustedTimeoutValue( uint64 timeout )
{
    return timeout;
}

void RtcSetTimeout( uint64 timeout )
{
#if 0
	RTC_SetUnixTime(timeout);
#endif
    //RTC_DATE_TIME current;
    //RTC_GetDateAndTime(&current);
    //current.time += timeout;
    //RTC_SetAlarmDateAndTime(&current);
    //RTC_SetAlarmHandler(TimerIrqHandler);
    BoardDisableIrq();
    sys_timeout = (uint32_t)timeout;
    current_times = 0;
    BoardEnableIrq();
}

TimerTime_t RtcGetElapsedAlarmTime( void )
{
    return(RTC_GetUnixTime());
}

TimerTime_t RtcGetTimerValue( void )
{
    return(RTC_GetUnixTime());
}

TimerTime_t RtcComputeElapsedTime( TimerTime_t eventInTime )
{
    TimerTime_t now = RTC_GetUnixTime();
    return (now = eventInTime);
#if 0
    TimerTime_t elapsedTime = 0;

    if( eventInTime == 0 )
    {
        return 0;
    }
    
    elapsedTime = RTC_GetUnixTime();

    if( elapsedTime < eventInTime ) 
    {  
        return elapsedTime;
    }
    else
    {
        return eventInTime;
    }
#endif
}

void DelayMsMcu( uint32_t ms )
{
    mdelay(ms);
}

static const double huge = 1.0e300;
#define __HI(x) *(1+(int*)&x)
#define __LO(x) *(int*)&x
double floor(double x)
{
	int i0,i1,j0;
	unsigned i,j;
	i0 =  __HI(x);
	i1 =  __LO(x);
	j0 = ((i0>>20)&0x7ff)-0x3ff;
	if(j0<20) {
	    if(j0<0) { 	/* raise inexact if x != 0 */
		if(huge+x>0.0) {/* return 0*sign(x) if |x|<1 */
		    if(i0>=0) {i0=i1=0;} 
		    else if(((i0&0x7fffffff)|i1)!=0)
			{ i0=0xbff00000;i1=0;}
		}
	    } else {
		i = (0x000fffff)>>j0;
		if(((i0&i)|i1)==0) return x; /* x is integral */
		if(huge+x>0.0) {	/* raise inexact flag */
		    if(i0<0) i0 += (0x00100000)>>j0;
		    i0 &= (~i); i1=0;
		}
	    }
	} else if (j0>51) {
	    if(j0==0x400) return x+x;	/* inf or NaN */
	    else return x;		/* x is integral */
	} else {
	    i = ((unsigned)(0xffffffff))>>(j0-20);
	    if((i1&i)==0) return x;	/* x is integral */
	    if(huge+x>0.0) { 		/* raise inexact flag */
		if(i0<0) {
		    if(j0==20) i0+=1; 
		    else {
			j = i1+(1<<(52-j0));
			if(j<i1) i0 +=1 ; 	/* got a carry */
			i1=j;
		    }
		}
		i1 &= (~i);
	    }
	}
	__HI(x) = i0;
	__LO(x) = i1;
	return x;
}

double ceil(double x)
{
	int i0,i1,j0;
	unsigned i,j;
	i0 =  __HI(x);
	i1 =  __LO(x);
	j0 = ((i0>>20)&0x7ff)-0x3ff;
	if(j0<20) {
	    if(j0<0) { 	/* raise inexact if x != 0 */
		if(huge+x>0.0) {/* return 0*sign(x) if |x|<1 */
		    if(i0<0) {i0=0x80000000;i1=0;} 
		    else if((i0|i1)!=0) { i0=0x3ff00000;i1=0;}
		}
	    } else {
		i = (0x000fffff)>>j0;
		if(((i0&i)|i1)==0) return x; /* x is integral */
		if(huge+x>0.0) {	/* raise inexact flag */
		    if(i0>0) i0 += (0x00100000)>>j0;
		    i0 &= (~i); i1=0;
		}
	    }
	} else if (j0>51) {
	    if(j0==0x400) return x+x;	/* inf or NaN */
	    else return x;		/* x is integral */
	} else {
	    i = ((unsigned)(0xffffffff))>>(j0-20);
	    if((i1&i)==0) return x;	/* x is integral */
	    if(huge+x>0.0) { 		/* raise inexact flag */
		if(i0>0) {
		    if(j0==20) i0+=1; 
		    else {
			j = i1 + (1<<(52-j0));
			if(j<i1) i0+=1;	/* got a carry */
			i1 = j;
		    }
		}
		i1 &= (~i);
	    }
	}
	__HI(x) = i0;
	__LO(x) = i1;
	return x;
}

static const double TWO52[2]={
  4.50359962737049600000e+15, /* 0x43300000, 0x00000000 */
 -4.50359962737049600000e+15, /* 0xC3300000, 0x00000000 */
};

double rint(double x)
{
	int i0,j0,sx;
	unsigned i,i1;
	double w,t;
	i0 =  __HI(x);
	sx = (i0>>31)&1;
	i1 =  __LO(x);
	j0 = ((i0>>20)&0x7ff)-0x3ff;
	if(j0<20) {
	    if(j0<0) { 	
		if(((i0&0x7fffffff)|i1)==0) return x;
		i1 |= (i0&0x0fffff);
		i0 &= 0xfffe0000;
		i0 |= ((i1|-i1)>>12)&0x80000;
		__HI(x)=i0;
	        w = TWO52[sx]+x;
	        t =  w-TWO52[sx];
	        i0 = __HI(t);
	        __HI(t) = (i0&0x7fffffff)|(sx<<31);
	        return t;
	    } else {
		i = (0x000fffff)>>j0;
		if(((i0&i)|i1)==0) return x; /* x is integral */
		i>>=1;
		if(((i0&i)|i1)!=0) {
		    if(j0==19) i1 = 0x40000000; else
		    i0 = (i0&(~i))|((0x20000)>>j0);
		}
	    }
	} else if (j0>51) {
	    if(j0==0x400) return x+x;	/* inf or NaN */
	    else return x;		/* x is integral */
	} else {
	    i = ((unsigned)(0xffffffff))>>(j0-20);
	    if((i1&i)==0) return x;	/* x is integral */
	    i>>=1;
	    if((i1&i)!=0) i1 = (i1&(~i))|((0x40000000)>>(j0-20));
	}
	__HI(x) = i0;
	__LO(x) = i1;
	w = TWO52[sx]+x;
	return w-TWO52[sx];
}

void BoardInitMcu( void )
{
    DBG_PRINTF("======init ASR board======\r\n");
    SX126xIoInit();
}