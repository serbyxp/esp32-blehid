#ifndef FREERTOS_TASK_H
#define FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t xTicksToDelay);

#endif // FREERTOS_TASK_H
