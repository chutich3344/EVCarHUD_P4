#pragma once
#include "freertos/FreeRTOS.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *b) { return (SemaphoreHandle_t)b; }
static inline int xSemaphoreTake(SemaphoreHandle_t h, uint32_t t) { (void)h; (void)t; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t h) { (void)h; return 1; }
