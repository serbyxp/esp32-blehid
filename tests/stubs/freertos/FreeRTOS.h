#ifndef FREERTOS_FREERTOS_H
#define FREERTOS_FREERTOS_H

#include <stdint.h>

#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#define portMAX_DELAY 0xFFFFFFFF

#endif // FREERTOS_FREERTOS_H
