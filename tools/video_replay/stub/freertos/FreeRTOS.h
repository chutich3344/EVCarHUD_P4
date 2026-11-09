/* Gia lap toi thieu FreeRTOS cho cong cu PC (mot luong, khoa khong lam gi) */
#pragma once
#include <stdint.h>
#define portMAX_DELAY 0xffffffffu
typedef int StaticSemaphore_t;
typedef int *SemaphoreHandle_t;
