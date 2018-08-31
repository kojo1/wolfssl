
/*
 * Auto generated Run-Time-Environment Component Configuration File
 *      *** Do not modify ! ***
 *
 * Project: 'wolfsslFull' 
 * Target:  'STM32F207 Flash' 
 */

#ifndef RTE_COMPONENTS_H
#define RTE_COMPONENTS_H


/*
 * Define the Device Header File: 
 */
#define CMSIS_device_header "stm32f2xx.h"

#define RTE_CMSIS_RTOS                  /* CMSIS-RTOS */
        #define RTE_CMSIS_RTOS_RTX              /* CMSIS-RTOS Keil RTX */
#define RTE_Compiler_IO_File            /* Compiler I/O: File */
          #define RTE_Compiler_IO_File_FS         /* Compiler I/O: File (File System) */
#define RTE_Compiler_IO_STDERR          /* Compiler I/O: STDERR */
          #define RTE_Compiler_IO_STDERR_ITM      /* Compiler I/O: STDERR ITM */
#define RTE_Compiler_IO_STDIN           /* Compiler I/O: STDIN */
          #define RTE_Compiler_IO_STDIN_ITM       /* Compiler I/O: STDIN ITM */
#define RTE_Compiler_IO_STDOUT          /* Compiler I/O: STDOUT */
          #define RTE_Compiler_IO_STDOUT_ITM      /* Compiler I/O: STDOUT ITM */
#define RTE_DEVICE_FRAMEWORK_CLASSIC
#define RTE_DEVICE_HAL_COMMON
#define RTE_DEVICE_HAL_CORTEX
#define RTE_DEVICE_HAL_DMA
#define RTE_DEVICE_HAL_ETH
#define RTE_DEVICE_HAL_GPIO
#define RTE_DEVICE_HAL_RCC
#define RTE_DEVICE_HAL_SRAM
#define RTE_DEVICE_STARTUP_STM32F2XX    /* Device Startup for STM32F2 */
#define RTE_Drivers_ETH_MAC0            /* Driver ETH_MAC0 */
#define RTE_Drivers_MCI0                /* Driver MCI0 */
#define RTE_Drivers_PHY_ST802RT1        /* Driver PHY ST802RT1 */
#define RTE_FileSystem_Core             /* File System Core */
          #define RTE_FileSystem_LFN              /* File System with Long Filename support */
          #define RTE_FileSystem_Release          /* File System Release Version */
#define RTE_FileSystem_Drive_MC_0       /* File System Memory Card Drive 0 */
#define RTE_Network_Core                /* Network Core */
          #define RTE_Network_IPv4                /* Network IPv4 Stack */
          #define RTE_Network_Release             /* Network Release Version */
#define RTE_Network_DNS_Client          /* Network DNS Client */
#define RTE_Network_Interface_ETH_0     /* Network Interface ETH 0 */
#define RTE_Network_Socket_BSD          /* Network Socket BSD */
#define RTE_Network_Socket_TCP          /* Network Socket TCP */
#define RTE_Network_Socket_UDP          /* Network Socket UDP */

#endif /* RTE_COMPONENTS_H */
