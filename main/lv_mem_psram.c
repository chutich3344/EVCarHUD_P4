/* Bo nho LVGL dat het trong PSRAM (CONFIG_LV_USE_CUSTOM_MALLOC).
 *
 * 16/11 (1.0.55): truoc day LVGL dung malloc he thong; voi CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096 moi doi tuong nho
 * (nhan, khung, kieu chu: ~1.800 khoi, ~60-80 KB) deu nam trong RAM chip 768 KB. Them o xADAS + nut Mi Mi + hang dan duong
 * gop lam RAM chip het cho camera USB (bo giai ma JPEG, USB Host, ngan xep luong camera 16 KB) -> Cai dat bao khong nhan
 * camera. Doi tuong giao dien khong can toc do RAM chip (ve vao bo dem rieng), nen dat het sang PSRAM. */
#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "lvgl.h"

#define CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) { (void)mem; (void)bytes; return NULL; }
void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }

void *lv_malloc_core(size_t size)
{
    void *p = heap_caps_malloc(size, CAPS);
    return p ? p : malloc(size);                             /* het PSRAM (khong xay ra) thi dung RAM chip */
}

void *lv_realloc_core(void *p, size_t new_size)
{
    if (p && !esp_ptr_external_ram(p)) return realloc(p, new_size);
    void *q = heap_caps_realloc(p, new_size, CAPS);
    if (!q && new_size) {
        q = malloc(new_size);
        if (q && p) {
            size_t old = heap_caps_get_allocated_size(p);
            memcpy(q, p, old < new_size ? old : new_size);
            heap_caps_free(p);
        }
    }
    return q;
}

void lv_free_core(void *p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    multi_heap_info_t i;
    heap_caps_get_info(&i, CAPS);
    memset(mon_p, 0, sizeof(*mon_p));
    mon_p->total_size = i.total_free_bytes + i.total_allocated_bytes;
    mon_p->free_size = i.total_free_bytes;
    mon_p->free_biggest_size = i.largest_free_block;
    mon_p->used_cnt = i.allocated_blocks;
    mon_p->free_cnt = i.free_blocks;
    mon_p->used_pct = mon_p->total_size ? 100 - (uint8_t)(i.total_free_bytes * 100 / mon_p->total_size) : 0;
}

lv_result_t lv_mem_test_core(void) { return heap_caps_check_integrity(CAPS, false) ? LV_RESULT_OK : LV_RESULT_INVALID; }

/* main.c goi de chac chan tep nay duoc lien ket (thu vien tinh) */
void lv_mem_psram_link(void) {}
