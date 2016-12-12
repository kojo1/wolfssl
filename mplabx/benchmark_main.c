/* benchmark_main.c
 *
 * Copyright (C) 2006-2016 wolfSSL Inc.
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

#include <wolfssl/wolfcrypt/settings.h>

#if defined(WOLFSSL_MICROCHIP_PIC32MZ)
    #define MICROCHIP_PIC32
    #include <xc.h>
    #pragma config ICESEL = ICS_PGx2
            /* ICE/ICD Comm Channel Select (Communicate on PGEC2/PGED2) */
    #include <stdio.h>
    #include <stdlib.h>
    #include "PIC32MZ-serial.h"
    #define  SYSTEMConfigPerformance(a) /* void out SYSTEMConfigPerformance(); */
#else
    #define PIC32_STARTER_KIT
    #define _SUPPRESS_PLIB_WARNING
    #define _DISABLE_OPENADC10_CONFIGPORT_WARNING
    #include <plib.h>
    #include <sys/appio.h>
    #define init_serial() /* void out init_serial() ; */
#endif

int benchmark_test(void) ;
/*
 * Main driver for wolfCrypt benchmarks.
 */
int main(int argc, char** argv) {
    init_serial() ;  /* initialize PIC32MZ serial I/O */
    SYSTEMConfigPerformance(80000000);
    DBINIT();
    printf("wolfCrypt Benchmark:\n");

    benchmark_test() ;

    printf("End of wolfCrypt Benchmark:\n");
    return 0;
}

