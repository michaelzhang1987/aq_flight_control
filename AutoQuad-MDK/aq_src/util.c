/*
    This file is part of AutoQuad.

    AutoQuad is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    AutoQuad is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with AutoQuad.  If not, see <http://www.gnu.org/licenses/>.

    Copyright © 2011-2014  Bill Nesbitt
*/

#include "aq.h"
#include "util.h"
#include "aq_timer.h"
#include "flash.h"
#include "rcc.h"
#include "comm.h"
#include "aq_mavlink.h"
#include "aq_timer.h"
#include "aq_version.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>





uint32_t heapUsed, heapHighWater, dataSramUsed;

uint32_t *ccmHeap[UTIL_CCM_HEAP_SIZE] __attribute__((section(".ccm"))) __attribute__((aligned (8)));


void *aqCalloc(size_t count, size_t size)
{
    char *addr = 0;

    if (count * size) {
        addr = rt_calloc(count, size);

//        heapUsed += count * size;
//        if (heapUsed > heapHighWater)
//            heapHighWater = heapUsed;

        if (addr == 0)
            AQ_NOTICE("Out of heap memory!\n");
    }

    return addr;
}


#ifdef UTIL_STACK_CHECK
int32_t numStacks;
void *stackPointers[UTIL_STACK_CHECK] __attribute__((section(".ccm")));
uint16_t stackSizes[UTIL_STACK_CHECK] __attribute__((section(".ccm")));
uint16_t stackFrees[UTIL_STACK_CHECK] __attribute__((section(".ccm")));
char *stackNames[UTIL_STACK_CHECK] __attribute__((section(".ccm")));

//void utilStackCheck(void) {
//    int i, j;

//    for (i = 0; i < numStacks; i++) {
//        for (j = 0; j < stackSizes[i]; j++)
//            if (*(char *)(stackPointers[i]+j) != 0xFF)
//                break;
//        if (stackFrees[i] > j)
//            stackFrees[i] = j;
//        if (j < 16) {
//            AQ_PRINTF("Possible stack overflow [%s]!\n", stackNames[i]);
//        }
//    }
//}

uint16_t utilGetStackFree(const char *stackName) {
    uint16_t stkFree = 0, i;

    for (i=0; i < numStacks; i++) {
        if ( !strncmp(stackName, stackNames[i], 20) ) {
            stkFree = stackFrees[i];
            break;
        }
    }

    return stkFree;
}

#endif

//void *aqCalloc(size_t count, size_t size) {
//    char *addr = 0;

//    if (count * size) {
//        addr = calloc(count, size);

//        heapUsed += count * size;
//        if (heapUsed > heapHighWater)
//            heapHighWater = heapUsed;

//        if (addr == 0)
//            AQ_NOTICE("Out of heap memory!\n");
//    }

//    return addr;
//}

void aqFree(void *ptr, size_t count, size_t size) {
    if (ptr) {
        rt_free(ptr);
        //heapUsed -= count * size;
    }
}

// allocates memory from 64KB CCM
void *aqDataCalloc(uint16_t count, uint16_t size) {
    uint32_t words;

    // round up to word size
    words = (count*size + sizeof(int)-1) / sizeof(int);

    if ((dataSramUsed + words) > UTIL_CCM_HEAP_SIZE) {
        AQ_NOTICE("Out of data SRAM!\n");
    }
    else {
        dataSramUsed += words;
    }

    return (void *)(ccmHeap + dataSramUsed - words);
}

// size in words
unsigned int *aqStackInit(uint16_t size, char *name) {
    unsigned int *sp;

    // use memory in the CCM
    sp = (unsigned int *)aqDataCalloc(1, size*4);

    // fill memory with pattern to ease overflow detection
    //kevin memset(sp, 0xFF, size*4);

#ifdef UTIL_STACK_CHECK
    stackPointers[numStacks] = sp;
    stackSizes[numStacks] = size*4;
    stackFrees[numStacks] = stackSizes[numStacks];
    stackNames[numStacks] = name;
    numStacks++;
#endif

    return sp;
}

void delayMicros(unsigned long t) {
    t = t + timerMicros();

    while (timerMicros() < t)
        ;
}

// delay for given milli seconds
void delay(unsigned long t) {
    delayMicros(t * 1000);
}

void utilSerialNoString(void) {
    AQ_PRINTF("AQ S/N: %08X-%08X-%08X\n", flashSerno(2), flashSerno(1), flashSerno(0));
}

void utilVersionString(void) {
    AQ_PRINTF("AQ FW ver: %d.%d.%d-%s, HW ver: %d.%d\n", FIMRWARE_VER_MAJ, FIMRWARE_VER_MIN, FIMRWARE_VER_BLD, FIMRWARE_VER_STR, BOARD_VERSION, BOARD_REVISION);
}

void info(void) {
    utilSerialNoString();

#ifdef USE_MAVLINK
    AQ_PRINTF("Mavlink SYS ID: %d\n", flashSerno(0) % 250);
#endif

    AQ_PRINTF("SYS Clock: %u MHz\n", rccClocks.SYSCLK_Frequency / 1000000);
    //AQ_PRINTF("%u/%u heap used/high water\n", heapUsed, heapHighWater);
    AQ_PRINTF("%u of %u CCM heap used\n", dataSramUsed * sizeof(int), UTIL_CCM_HEAP_SIZE * sizeof(int));

    yield(100);
    utilVersionString();
}

void utilFilterReset(utilFilter_t *f, float setpoint) {
    f->z1 = setpoint;
}

void utilFilterReset3(utilFilter_t *f, float setpoint) {
    utilFilterReset(&f[0], setpoint);
    utilFilterReset(&f[1], setpoint);
    utilFilterReset(&f[2], setpoint);
}

// larger tau, smoother filter
void utilFilterInit(utilFilter_t *f, float dt, float tau, float setpoint) {
    f->tc = dt / tau;
    utilFilterReset(f, setpoint);
}

void utilFilterInit3(utilFilter_t *f, float dt, float tau, float setpoint) {
    utilFilterInit(&f[0], dt, tau, setpoint);
    utilFilterInit(&f[1], dt, tau, setpoint);
    utilFilterInit(&f[2], dt, tau, setpoint);
}

float utilFilter(utilFilter_t *f, float signal) {
    register float z1;

    z1 = f->z1 + (signal - f->z1) * f->tc;

    f->z1 = z1;

    return z1;
}

float utilFilter3(utilFilter_t *f, float signal) {
    return utilFilter(&f[0], utilFilter(&f[1], utilFilter(&f[2], signal)));
}

float utilFirFilter(utilFirFilter_t *f, float newValue) {
    float result = 0.0f;
    int i;

    f->data[f->i] = newValue;
    f->i = (f->i + 1) % f->n;

    for (i = 0; i < f->n; i++)
        result += f->window[i] * f->data[(f->i + i) % f->n];

    return result;
}

void utilFirFilterInit(utilFirFilter_t *f, const float *window, float *buffer, uint8_t n) {
    int i;

    f->window = window;
    f->data = buffer;
    f->n = n;
    f->i = 0;

    for (i = 0; i < n; i++)
        f->data[i] = 0.0f;
}

// unused
/*
int ftoa(char *buf, float f, unsigned int digits) {
    int bl = 0;
    float whole, frac;
    long long llfrac;

    // handle infinite values
    if (isinf(f)) {
        strcpy(buf, "INF");
        return 3;
    }
    // handle Not a Number
    else if (isnan(f)) {
        strcpy(buf, "NaN");
        return 3;
    }
    else {

	frac = modff(f, &whole);
	//index = sprintf(buf, "%ld", (long)whole);
	bl = strlen(ltoa((long)whole, buf, 10));

	if (digits && frac) {
	    frac *= powf(10.0f, digits + 1);
	    llfrac = (long long)fabs(frac);
	    if (llfrac % 10 > 4)
		llfrac += 10;
	    bl += sprintf(&buf[bl], ".%0*lld", digits, llfrac / 10);
	}

	return bl;
    }
}
*/
