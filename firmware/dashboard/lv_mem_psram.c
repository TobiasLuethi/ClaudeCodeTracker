/* Speicher für LVGL (Objekte, Stile, Schriftcache von tiny_ttf) aus dem PSRAM.
 * Der interne RAM bleibt so für WLAN/lwIP, DMA und Treiber frei; vorher reichte er nicht
 * mehr, und der DHCP-Dienst des Einrichtungs-Hotspots konnte keine Adressen vergeben.
 * Aktiv über LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM in lv_conf.h. */
#include <lvgl.h>
#include <esp_heap_caps.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) {
  LV_UNUSED(mem);
  LV_UNUSED(bytes);
  return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void *lv_malloc_core(size_t size) {
  void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

void *lv_realloc_core(void *p, size_t new_size) {
  void *n = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return n ? n : heap_caps_realloc(p, new_size, MALLOC_CAP_8BIT);
}

void lv_free_core(void *p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p) { LV_UNUSED(mon_p); }

lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

#endif
