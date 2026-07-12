/*****************************************************************************
  | File        :   LVGL_Driver.c
  
  | help        : 
    The provided LVGL library file must be installed first
******************************************************************************/
#include "LVGL_Driver.h"

// === Anti-tearing VSYNC ===
//  1 = VSYNC synchronized: zero tearing but ~15 FPS (waits for vertical blanking)
//  0 = No synchronization: ~30-50 FPS but slight tearing during rapid needle movements
// NOTE (1er juillet 2026) : gel total de l'ecran reproduit avec le direct_mode +
// double framebuffer (LVGL_ANTITEAR_VSYNC=1) ET avec le mode partiel EN CHUNKS
// (buffers < pleine hauteur, 2 flush par refresh). Cause reelle : un widget de
// l'ecran "main" ne supporte pas d'etre redessine en plusieurs morceaux (chunk
// boundary a mi-ecran) -> gele des la 1ere frame complete. Fix : un SEUL buffer
// PLEIN ECRAN (voir Lvgl_Init, plus de decoupage en bandes de 240 lignes) ; garder
// VSYNC=0 (pas de direct_mode) qui reste la config la plus simple/robuste testee.
#define LVGL_ANTITEAR_VSYNC 0

// Anti-tearing synchronization semaphores (defined in Display_ST7701.cpp)
extern SemaphoreHandle_t sem_vsync_end;
extern SemaphoreHandle_t sem_gui_ready;
extern esp_lcd_panel_handle_t panel_handle;   // Created in Display_ST7701.cpp

lv_disp_drv_t disp_drv;

static lv_disp_draw_buf_t draw_buf;
void* buf1 = NULL;
void* buf2 = NULL;
// static lv_color_t buf1[ LVGL_BUF_LEN ];
// static lv_color_t buf2[ LVGL_BUF_LEN ];
// static lv_color_t* buf1 = (lv_color_t*) heap_caps_malloc(LVGL_BUF_LEN, MALLOC_CAP_SPIRAM);
// static lv_color_t* buf2 = (lv_color_t*) heap_caps_malloc(LVGL_BUF_LEN, MALLOC_CAP_SPIRAM);
    


/* Serial debugging */
void Lvgl_print(const char * buf)
{
    // Serial.printf(buf);
    // Serial.flush();
}

/*  Display flushing 
    Displays LVGL content on the LCD
    This function implements associating LVGL data to the LCD screen
*/
// --- Performance metrics telemetry (read by main.cpp for [PERF] log output) ---
volatile uint32_t g_perfFlushCnt = 0, g_perfFlushUs = 0, g_perfWaitUs = 0, g_perfPx = 0;

void Lvgl_Display_LCD( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p )
{
  uint32_t t0 = micros();
#if LVGL_ANTITEAR_VSYNC
  // Anti-tearing: wait for VSYNC before writing into the framebuffer
  if (sem_gui_ready != NULL && sem_vsync_end != NULL) {
    xSemaphoreGive(sem_gui_ready);
    xSemaphoreTake(sem_vsync_end, portMAX_DELAY);
  }
#endif
  LCD_addWindow(area->x1, area->y1, area->x2, area->y2, ( uint8_t *)&color_p->full);
  g_perfWaitUs += micros() - t0;
  g_perfFlushCnt = g_perfFlushCnt + 1;
  lv_disp_flush_ready( disp_drv );
}
/*Read the touchpad*/
void Lvgl_Touchpad_Read( lv_indev_drv_t * indev_drv, lv_indev_data_t * data )
{
  // Tactile desactive : Touch_Read_Data() fait un acces I2C partage avec Driver_Loop
  // (IMU/RTC/batterie sur l'autre coeur, meme bus, sans mutex). Le tactile n'est
  // utilise nulle part dans l'UI (tout est pilote par les encodeurs) -> on saute
  // la lecture I2C pour eliminer la collision qui gelait l'ecran.
  // Touch_Read_Data();
  if (touch_data.points != 0x00) {
    data->point.x = touch_data.x;
    data->point.y = touch_data.y;
    data->state = LV_INDEV_STATE_PR;
    printf("LVGL : X=%u Y=%u points=%d\r\n",  touch_data.x , touch_data.y,touch_data.points);
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
  if (touch_data.gesture != NONE ) {    
  }
  touch_data.x = 0;
  touch_data.y = 0;
  touch_data.points = 0;
  touch_data.gesture = NONE;
}
void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}
void Lvgl_Init(void)
{
  lv_init();
  // Anti-tearing semaphores (sync flush <-> VSYNC)
  sem_vsync_end = xSemaphoreCreateBinary();
  sem_gui_ready = xSemaphoreCreateBinary();
  // Full-screen single buffer (no row-chunking): a partial buffer (< full height,
  // 2 flushes per refresh) reliably froze the screen on the first full-screen
  // render, see note above. One full buffer = one flush per refresh, no chunk
  // boundary -> stable. Costs ~450KB PSRAM, negligible on the 8MB module.
  buf1 = heap_caps_malloc(ESP_PANEL_LCD_WIDTH * ESP_PANEL_LCD_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  buf2 = NULL;
  printf("[LVGL] buf1=%p buf2=%p\n", buf1, buf2);
  lv_disp_draw_buf_init( &draw_buf, buf1, buf2, ESP_PANEL_LCD_WIDTH * ESP_PANEL_LCD_HEIGHT );

  /*Initialize the display*/
  lv_disp_drv_init( &disp_drv );
  /*Change the following line to your display resolution*/
  disp_drv.hor_res = LCD_HEIGHT;
  disp_drv.ver_res = LCD_WIDTH;
  disp_drv.flush_cb = Lvgl_Display_LCD;
  /* Partial mode: neither full_refresh nor direct_mode */
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register( &disp_drv );

  /*Initialize the (dummy) input device driver*/
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init( &indev_drv );
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = Lvgl_Touchpad_Read;
  lv_indev_drv_register( &indev_drv );

  /* Create simple label */
  lv_obj_t *label = lv_label_create( lv_scr_act() );
  lv_label_set_text( label, "Hello Ardino and LVGL!");
  lv_obj_align( label, LV_ALIGN_CENTER, 0, 0 );

  const esp_timer_create_args_t lvgl_tick_timer_args = {
    .callback = &example_increase_lvgl_tick,
    .name = "lvgl_tick"
  };
  esp_timer_handle_t lvgl_tick_timer = NULL;
  esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
  esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);

}
volatile uint32_t g_perfHandlerUs = 0, g_perfHandlerCnt = 0;
void Lvgl_Loop(void)
{
  uint32_t t = micros();
  lv_timer_handler(); /* let the GUI do its work */
  g_perfHandlerUs += micros() - t;
  g_perfHandlerCnt = g_perfHandlerCnt + 1;
  // vTaskDelay(pdMS_TO_TICKS(5));
}

// Force LVGL a redessiner TOUT l'ecran au prochain lv_timer_handler.
// En mode partiel, seules les zones qui changent sont redessinees ; apres une
// corruption du framebuffer (ex : arret du WiFi qui coupe le cache PSRAM), les
// zones statiques restent abimees tant qu'on ne les invalide pas explicitement.
void Lvgl_ForceFullRedraw(void)
{
  lv_obj_invalidate(lv_scr_act());
}
