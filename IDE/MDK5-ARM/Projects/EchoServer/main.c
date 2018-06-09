/* main.c
 *
 * Copyright (C) 2006-2017 wolfSSL Inc.
 *
 * This file is part of wolfSSL.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include "wolfssl/wolfcrypt/settings.h"

#include "cmsis_os.h"                 /* CMSIS RTOS definitions             */
#include "rl_net.h"                      /* Network definitions                */
#include <time.h>

#if defined(STM32F7xx)
#include "stm32f7xx_hal.h"
#elif defined(STM32F4xx)
#include "stm32f4xx_hal.h"
#elif defined(STM32F2xx)
#include "stm32f2xx_hal.h"
#endif

//-------- <<< Use Configuration Wizard in Context Menu >>> -----------------

//   <h>Server parameter
//   ====================

//   <s.6>Port
//   <i> Default: "11111"
#define SERVER_PORT "11111"
//   </h>

//   <h>Protocol
//   ====================

//   <o>SSL/TLS Version<0=> SSL3 <1=> TLS1.0 <2=> TLS1.1 <3=> TLS1.2 <4=> TLS1.3
#define TLS_VER 3

//   <s.2>Other option
#define OTHER_OPTIONS ""
//   </h>

//   <h>RTC: for validate certificate date
//    <o>Year <1970-2099>
#define RTC_YEAR 2018
//    <o>Month <1=>Jan<2=>Feb<3=>Mar<4=>Apr<5=>May<6=>Jun<7=>Jul<8=>Aut<9=>Sep<10=>Oct<11=>Nov<12=>Dec
#define RTC_MONTH 1
//    <o>Day <1-31>
#define RTC_DAY 1
//    </h>

//------------- <<< end of configuration section >>> -----------------------

static void SystemClock_Config (void) {
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_PeriphCLKInitTypeDef RCC_PeriphClkInitStruct;


  /* Enable HSE Oscillator and activate PLL with HSE as source */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_OFF;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 432;  
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 9;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  /* Activate the OverDrive to reach the 216 MHz Frequency */
  HAL_PWREx_EnableOverDrive();

  /* Select PLL as system clock source and configure the HCLK, PCLK1 and PCLK2 clocks dividers */
  RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;  
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;  
  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7);
    
  /* Select 48MHz clock source as SDMMC1 clock */
  RCC_PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SDMMC1;
  RCC_PeriphClkInitStruct.Sdmmc1ClockSelection = RCC_SDMMC1CLKSOURCE_CLK48;
  HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphClkInitStruct); 
    
  #if defined(STM32_CRYPTO)
    __HAL_RCC_CRYP_CLK_ENABLE();
  #endif
  #if defined(STM32_HASH)
    __HAL_RCC_HASH_CLK_ENABLE();
  #endif      
  #if defined(STM32_RNG)
    __HAL_RCC_RNG_CLK_ENABLE();
  #endif
}

/**
  * Configure the MPU attributes as Write Through for SRAM1/2
  *   The Base Address is 0x20010000 since this memory interface is the AXI.
  *   The Region Size is 256KB, it is related to SRAM1 and SRAM2 memory size.
  */
static void MPU_Config (void) {
  MPU_Region_InitTypeDef MPU_InitStruct;
  
  /* Disable the MPU */
  HAL_MPU_Disable();

  /* Configure the MPU attributes as WT for SRAM */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress = 0x20010000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_256KB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress = 0x60000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_16MB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Enable the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
  * CPU L1-Cache enable
  */
static void CPU_CACHE_Enable (void) {

  /* Enable I-Cache */
  SCB_EnableICache();

  /* Enable D-Cache */
  SCB_EnableDCache();
}



/*-----------------------------------------------------------------------------
 *        Initialize a Flash Memory Card
 *----------------------------------------------------------------------------*/
#if !defined(NO_FILESYSTEM)
#include "rl_fs.h"                      /* FileSystem definitions             */

static void init_filesystem (void) {
  int32_t retv;

  retv = finit ("M0:");
  if (retv == fsOK) {
    retv = fmount ("M0:");
    if (retv == fsOK) {
      printf ("Drive M0 ready!\n");
    }
    else {
      printf ("Drive M0 mount failed(%d)!\n", retv);
    }
  }
  else {
    printf ("Drive M0 initialization failed!\n");
  }
}
#endif


void net_loop(void const *arg)
{
    while(1) {
        net_main ();
        osThreadYield ();
    }
}

osThreadDef(net_loop, osPriorityLow, 2, 0);

#ifdef RTE_CMSIS_RTOS_RTX
extern uint32_t os_time;
static  time_t epochTime;

uint32_t HAL_GetTick(void) { 
    return os_time; 
}

time_t time(time_t *t){
     return epochTime ;
}

void setTime(time_t t){
    epochTime = t;
}
#endif


#ifdef WOLFSSL_CURRTIME_OSTICK

#include <stdint.h>
extern uint32_t os_time;

double current_time(int reset)
{
      if(reset) os_time = 0 ;
      return (double)os_time /1000.0;
}

#else

#include <stdint.h>
#define DWT                 ((DWT_Type       *)     (0xE0001000UL)     )
typedef struct
{
  uint32_t CTRL;                    /*!< Offset: 0x000 (R/W)  Control Register                          */
  uint32_t CYCCNT;                  /*!< Offset: 0x004 (R/W)  Cycle Count Register                      */
} DWT_Type;

extern uint32_t SystemCoreClock ;

double current_time(int reset)
{
      if(reset) DWT->CYCCNT = 0 ;
      return ((double)DWT->CYCCNT/SystemCoreClock) ;
}
#endif

/*----------------------------------------------------------------------------
  Main Thread 'main': Run Network
 *---------------------------------------------------------------------------*/
#include <stdio.h>
typedef struct func_args {
    int    argc;
    char** argv;
} func_args;

extern void echoserver_test(func_args * args) ;

int myoptind = 0;
char* myoptarg = NULL;

int main (void) {
     static char *argv[] =
          {   "server" } ;
     static   func_args args  = { 1, argv } ;

    MPU_Config();                             /* Configure the MPU              */
    CPU_CACHE_Enable();                       /* Enable the CPU Cache           */
    HAL_Init();                               /* Initialize the HAL Library     */
    SystemClock_Config();                     /* Configure the System Clock     */

    #if !defined(NO_FILESYSTEM)
    init_filesystem ();
    #endif
    net_initialize ();

    #if defined(DEBUG_WOLFSSL)
         printf("Turning ON Debug message\n") ;
         wolfSSL_Debugging_ON() ;
    #endif

    setTime((RTC_YEAR-1970)*365*24*60*60 + RTC_MONTH*30*24*60*60 + RTC_DAY*24*60*60);

    osThreadCreate (osThread(net_loop), NULL);

    echoserver_test(&args) ;
    printf("echoserver: Terminated\n") ;
    while(1)
        osDelay(1000);

}

