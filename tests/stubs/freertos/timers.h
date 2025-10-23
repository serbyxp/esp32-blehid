#ifndef FREERTOS_TIMERS_H
#define FREERTOS_TIMERS_H

#include "freertos/FreeRTOS.h"

typedef void *TimerHandle_t;

typedef void (*TimerCallbackFunction_t)(TimerHandle_t xTimer);

TimerHandle_t xTimerCreate(const char *pcTimerName, TickType_t xTimerPeriod, int uxAutoReload, void *pvTimerID, TimerCallbackFunction_t pxCallbackFunction);
int xTimerIsTimerActive(TimerHandle_t xTimer);
int xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait);
int xTimerChangePeriod(TimerHandle_t xTimer, TickType_t xNewPeriod, TickType_t xTicksToWait);
int xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait);

#endif // FREERTOS_TIMERS_H
