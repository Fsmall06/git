#include <stdio.h>

#include "lcd_service.h"

#include "lcd.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "lvgl.h"

#define LCD_LVGL_DRAW_BUFFER_LINES 1U
#define LCD_LVGL_DRAW_BUFFER_PIXELS (LCD_H_RES * LCD_LVGL_DRAW_BUFFER_LINES)
#define LCD_LVGL_TASK_PRIORITY 1
#define LCD_LVGL_TASK_STACK 4096
#define LCD_LVGL_TIMER_PERIOD_MS 20
#define LCD_LVGL_MEM_LOG_PERIOD_MS 30000U
#define LCD_DASHBOARD_SNAPSHOT_REFRESH_MS 100U
#define LCD_DASHBOARD_DATA_REFRESH_MS 1000LL
#define LCD_DASHBOARD_LOG_PERIOD_MS 10000LL
#define LCD_DASHBOARD_MARGIN_X 12
#define LCD_DASHBOARD_TEXT_WIDTH (LCD_H_RES - (LCD_DASHBOARD_MARGIN_X * 2))
#define LCD_CAT_ANIMATION_PERIOD_MS 100U
#define LCD_CAT_WAKE_FRAMES 6U
#define LCD_CAT_RECORDING_FRAMES 6U
#define LCD_CAT_PLAY_MOUTH_PERIOD_FRAMES 4U
#define LCD_CAT_IMAGE_WIDTH 64U
#define LCD_CAT_IMAGE_HEIGHT 48U
#define LCD_CAT_IMAGE_STRIDE 32U
#define LCD_CAT_IMAGE_PALETTE_BYTES 64U
#define LCD_CAT_IMAGE_DATA_BYTES (LCD_CAT_IMAGE_PALETTE_BYTES + \
                                  (LCD_CAT_IMAGE_STRIDE * LCD_CAT_IMAGE_HEIGHT))
#define LCD_CAT_IMAGE_X 88
#define LCD_CAT_IMAGE_Y 172
#define LCD_VOICE_CAT_IMAGE_X 88
#define LCD_VOICE_CAT_IMAGE_Y 104

static const char *TAG = "LCD_LVGL";
static const char *MEM_TAG = "LVGL_MEM";
static lv_display_t *s_display;
static bool s_started;
static lv_obj_t *s_temp_label;
static lv_obj_t *s_humidity_label;
static lv_obj_t *s_pressure_label;
static lv_obj_t *s_gas_label;
static lv_obj_t *s_air_label;
static lv_obj_t *s_network_label;
static lv_obj_t *s_voice_label;
static lv_obj_t *s_dashboard_root;
static lv_obj_t *s_voice_root;
static lv_obj_t *s_cat_image;
static lv_timer_t *s_cat_animation_timer;
static lv_obj_t *s_voice_cat_image;
static lv_timer_t *s_voice_animation_timer;
static char s_temp_text[32];
static char s_humidity_text[32];
static char s_pressure_text[32];
static char s_gas_text[32];
static char s_air_text[32];
static char s_network_text[32];
static char s_voice_text[32];
static lcd_dashboard_snapshot_provider_t s_snapshot_provider;
static void *s_snapshot_provider_ctx;
static int64_t s_last_dashboard_log_ms;
static int64_t s_last_dashboard_data_update_ms;
static portMUX_TYPE s_snapshot_provider_lock = portMUX_INITIALIZER_UNLOCKED;
static lcd_dashboard_voice_state_t s_cat_voice_state;
static uint8_t s_cat_animation_frame;
static uint8_t s_cat_play_frame;
static bool s_cat_voice_state_valid;
static bool s_cat_play_mouth_open;
static bool s_cat_speaker_active;
static bool s_cat_disabled;
static const lv_image_dsc_t *s_cat_frame;
static lcd_dashboard_voice_state_t s_voice_ui_state;
static const lv_image_dsc_t *s_voice_cat_frame;
static uint8_t s_voice_animation_frame;
static uint8_t s_voice_play_frame;
static bool s_voice_play_mouth_open;
static bool s_voice_speaker_active;
static bool s_voice_ui_visible;
static bool s_voice_ui_disabled;

static void lcd_lvgl_log_memory(const char *stage);
static void lcd_cat_animation_timer_cb(lv_timer_t *timer);
static void lcd_voice_animation_timer_cb(lv_timer_t *timer);

static lv_obj_t *lcd_lvgl_create_label(lv_obj_t *parent,
                                        const char *text,
                                        lv_color_t color,
                                        int16_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text_static(label, text);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_width(label, LCD_DASHBOARD_TEXT_WIDTH);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, LCD_DASHBOARD_MARGIN_X, y);
    return label;
}

static const char *lcd_dashboard_air_state_name(lcd_dashboard_air_state_t state)
{
    switch (state) {
    case LCD_DASHBOARD_AIR_READY:
        return "READY";
    case LCD_DASHBOARD_AIR_DEGRADED:
        return "DEGRADED";
    case LCD_DASHBOARD_AIR_CALIBRATING:
        return "CALIBRATING";
    case LCD_DASHBOARD_AIR_INIT:
    default:
        return "INIT";
    }
}

static const char *lcd_dashboard_voice_state_name(lcd_dashboard_voice_state_t state)
{
    switch (state) {
    case LCD_DASHBOARD_VOICE_WAKE:
        return "WAKE";
    case LCD_DASHBOARD_VOICE_REC:
        return "REC";
    case LCD_DASHBOARD_VOICE_WAIT:
        return "WAIT";
    case LCD_DASHBOARD_VOICE_PLAY:
        return "PLAY";
    case LCD_DASHBOARD_VOICE_ERR:
        return "ERR";
    case LCD_DASHBOARD_VOICE_LISTEN:
    default:
        return "LISTEN";
    }
}

static void lcd_lvgl_set_static_text(lv_obj_t *label, const char *text)
{
    if (label != NULL && text != NULL) {
        lv_label_set_text_static(label, text);
    }
}

/* I4 source maps: index 0 is transparent, 1 white, 2 blue-gray, 3 mint, 4 pink.
 * Each logical grid cell is four physical pixels wide; rows are emitted twice. */
#define CAT_NIBBLE_PAIR_0 0x00
#define CAT_NIBBLE_PAIR_1 0x11
#define CAT_NIBBLE_PAIR_2 0x22
#define CAT_NIBBLE_PAIR_3 0x33
#define CAT_NIBBLE_PAIR_4 0x44
#define CAT_BLOCK(c) CAT_NIBBLE_PAIR_##c, CAT_NIBBLE_PAIR_##c,
#define CAT_ROW16(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) \
    CAT_BLOCK(a) CAT_BLOCK(b) CAT_BLOCK(c) CAT_BLOCK(d) \
    CAT_BLOCK(e) CAT_BLOCK(f) CAT_BLOCK(g) CAT_BLOCK(h) \
    CAT_BLOCK(i) CAT_BLOCK(j) CAT_BLOCK(k) CAT_BLOCK(l) \
    CAT_BLOCK(m) CAT_BLOCK(n) CAT_BLOCK(o) CAT_BLOCK(p)
#define CAT_REPEAT2(row) row row

/* The fixed 16 x 24 pixel-art grid is expanded to the 64 x 48 I4 source. */
#define CAT_ROW_CLEAR CAT_ROW16(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define CAT_ROW_EAR_TIP CAT_ROW16(0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0)
#define CAT_ROW_EAR_UPPER CAT_ROW16(0, 0, 1, 3, 1, 0, 0, 0, 0, 0, 0, 1, 3, 1, 0, 0)
#define CAT_ROW_EAR_BASE CAT_ROW16(0, 1, 3, 3, 1, 1, 0, 0, 0, 0, 1, 1, 3, 3, 1, 0)
#define CAT_ROW_FACE_TOP CAT_ROW16(0, 1, 3, 3, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 1, 0)
#define CAT_ROW_FACE CAT_ROW16(0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)
#define CAT_ROW_LISTEN_EYES CAT_ROW16(0, 1, 1, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 1, 1, 0)
#define CAT_ROW_OPEN_EYES CAT_ROW16(0, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 0)
#define CAT_ROW_ERROR_EYES_A CAT_ROW16(0, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 0)
#define CAT_ROW_ERROR_EYES_B CAT_ROW16(0, 1, 1, 1, 1, 1, 2, 1, 1, 2, 1, 1, 1, 1, 1, 0)
#define CAT_ROW_CHEEKS CAT_ROW16(2, 2, 1, 4, 4, 1, 1, 1, 1, 1, 1, 4, 4, 1, 2, 2)
#define CAT_ROW_NOSE CAT_ROW16(0, 2, 1, 1, 1, 1, 1, 4, 1, 1, 1, 1, 1, 1, 2, 0)
#define CAT_ROW_MOUTH_CLOSED CAT_ROW16(0, 0, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 0, 0)
#define CAT_ROW_MOUTH_OPEN CAT_ROW16(0, 0, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 0, 0)
#define CAT_ROW_FACE_LOWER CAT_ROW16(0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0)
#define CAT_ROW_PAW CAT_ROW16(0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)
#define CAT_ROW_PAW_PAD CAT_ROW16(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 0)
#define CAT_I4_PALETTE \
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, \
    0x54, 0x3d, 0x2a, 0xff, 0xa6, 0xd2, 0x79, 0xff, \
    0xb5, 0x9b, 0xf4, 0xff, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

static const uint8_t s_cat_listen_map[] __attribute__((aligned(4))) = {
    CAT_I4_PALETTE
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_EAR_TIP)
    CAT_REPEAT2(CAT_ROW_EAR_UPPER) CAT_REPEAT2(CAT_ROW_EAR_BASE)
    CAT_REPEAT2(CAT_ROW_FACE_TOP) CAT_REPEAT2(CAT_ROW_FACE)
    CAT_REPEAT2(CAT_ROW_FACE) CAT_REPEAT2(CAT_ROW_LISTEN_EYES)
    CAT_REPEAT2(CAT_ROW_FACE) CAT_REPEAT2(CAT_ROW_CHEEKS)
    CAT_REPEAT2(CAT_ROW_NOSE) CAT_REPEAT2(CAT_ROW_MOUTH_CLOSED)
    CAT_REPEAT2(CAT_ROW_FACE_LOWER) CAT_REPEAT2(CAT_ROW_FACE_LOWER)
    CAT_REPEAT2(CAT_ROW_FACE_LOWER) CAT_REPEAT2(CAT_ROW_FACE_LOWER)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
};

static const uint8_t s_cat_open_map[] __attribute__((aligned(4))) = {
    CAT_I4_PALETTE
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_EAR_TIP)
    CAT_REPEAT2(CAT_ROW_EAR_UPPER) CAT_REPEAT2(CAT_ROW_EAR_BASE)
    CAT_REPEAT2(CAT_ROW_FACE_TOP) CAT_REPEAT2(CAT_ROW_FACE)
    CAT_REPEAT2(CAT_ROW_FACE) CAT_REPEAT2(CAT_ROW_OPEN_EYES)
    CAT_REPEAT2(CAT_ROW_OPEN_EYES) CAT_REPEAT2(CAT_ROW_CHEEKS)
    CAT_REPEAT2(CAT_ROW_NOSE) CAT_REPEAT2(CAT_ROW_MOUTH_CLOSED)
    CAT_REPEAT2(CAT_ROW_FACE_LOWER) CAT_REPEAT2(CAT_ROW_FACE_LOWER)
    CAT_REPEAT2(CAT_ROW_FACE_LOWER) CAT_REPEAT2(CAT_ROW_FACE_LOWER)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
};

static const uint8_t s_cat_rec_map[] __attribute__((aligned(4))) = {
    CAT_I4_PALETTE
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_EAR_TIP)
    CAT_REPEAT2(CAT_ROW_EAR_UPPER) CAT_REPEAT2(CAT_ROW_EAR_BASE)
    CAT_REPEAT2(CAT_ROW_FACE_TOP) CAT_REPEAT2(CAT_ROW_FACE)
    CAT_REPEAT2(CAT_ROW_FACE) CAT_REPEAT2(CAT_ROW_OPEN_EYES)
    CAT_REPEAT2(CAT_ROW_OPEN_EYES) CAT_REPEAT2(CAT_ROW_CHEEKS)
    CAT_REPEAT2(CAT_ROW_NOSE) CAT_REPEAT2(CAT_ROW_MOUTH_CLOSED)
    CAT_REPEAT2(CAT_ROW_FACE_LOWER) CAT_REPEAT2(CAT_ROW_FACE_LOWER)
    CAT_REPEAT2(CAT_ROW_PAW) CAT_REPEAT2(CAT_ROW_PAW_PAD)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
};

static const uint8_t s_cat_play_map[] __attribute__((aligned(4))) = {
    CAT_I4_PALETTE
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_EAR_TIP)
    CAT_REPEAT2(CAT_ROW_EAR_UPPER) CAT_REPEAT2(CAT_ROW_EAR_BASE)
    CAT_REPEAT2(CAT_ROW_FACE_TOP) CAT_REPEAT2(CAT_ROW_FACE)
    CAT_REPEAT2(CAT_ROW_FACE) CAT_REPEAT2(CAT_ROW_OPEN_EYES)
    CAT_REPEAT2(CAT_ROW_OPEN_EYES) CAT_REPEAT2(CAT_ROW_CHEEKS)
    CAT_REPEAT2(CAT_ROW_NOSE) CAT_REPEAT2(CAT_ROW_MOUTH_OPEN)
    CAT_REPEAT2(CAT_ROW_MOUTH_OPEN) CAT_REPEAT2(CAT_ROW_FACE_LOWER)
    CAT_REPEAT2(CAT_ROW_FACE_LOWER) CAT_REPEAT2(CAT_ROW_FACE_LOWER)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
};

static const uint8_t s_cat_error_map[] __attribute__((aligned(4))) = {
    CAT_I4_PALETTE
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_EAR_TIP)
    CAT_REPEAT2(CAT_ROW_EAR_UPPER) CAT_REPEAT2(CAT_ROW_EAR_BASE)
    CAT_REPEAT2(CAT_ROW_FACE_TOP) CAT_REPEAT2(CAT_ROW_FACE)
    CAT_REPEAT2(CAT_ROW_FACE) CAT_REPEAT2(CAT_ROW_ERROR_EYES_A)
    CAT_REPEAT2(CAT_ROW_ERROR_EYES_B) CAT_REPEAT2(CAT_ROW_CHEEKS)
    CAT_REPEAT2(CAT_ROW_NOSE) CAT_REPEAT2(CAT_ROW_MOUTH_CLOSED)
    CAT_REPEAT2(CAT_ROW_FACE_LOWER) CAT_REPEAT2(CAT_ROW_FACE_LOWER)
    CAT_REPEAT2(CAT_ROW_FACE_LOWER) CAT_REPEAT2(CAT_ROW_FACE_LOWER)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
    CAT_REPEAT2(CAT_ROW_CLEAR) CAT_REPEAT2(CAT_ROW_CLEAR)
};

_Static_assert(sizeof(s_cat_listen_map) == LCD_CAT_IMAGE_DATA_BYTES, "listen frame size");
_Static_assert(sizeof(s_cat_open_map) == LCD_CAT_IMAGE_DATA_BYTES, "open frame size");
_Static_assert(sizeof(s_cat_rec_map) == LCD_CAT_IMAGE_DATA_BYTES, "recording frame size");
_Static_assert(sizeof(s_cat_play_map) == LCD_CAT_IMAGE_DATA_BYTES, "play frame size");
_Static_assert(sizeof(s_cat_error_map) == LCD_CAT_IMAGE_DATA_BYTES, "error frame size");

#define CAT_IMAGE_DSC(map) \
    { \
        .header = { \
            .magic = LV_IMAGE_HEADER_MAGIC, \
            .cf = LV_COLOR_FORMAT_I4, \
            .flags = 0, \
            .w = LCD_CAT_IMAGE_WIDTH, \
            .h = LCD_CAT_IMAGE_HEIGHT, \
            .stride = LCD_CAT_IMAGE_STRIDE, \
        }, \
        .data_size = sizeof(map), \
        .data = map, \
        .reserved = NULL, \
    }

static const lv_image_dsc_t s_cat_listen = CAT_IMAGE_DSC(s_cat_listen_map);
static const lv_image_dsc_t s_cat_open = CAT_IMAGE_DSC(s_cat_open_map);
static const lv_image_dsc_t s_cat_rec = CAT_IMAGE_DSC(s_cat_rec_map);
static const lv_image_dsc_t s_cat_play = CAT_IMAGE_DSC(s_cat_play_map);
static const lv_image_dsc_t s_cat_error = CAT_IMAGE_DSC(s_cat_error_map);

static void lcd_cat_disable_after_allocation_failure(void)
{
    if (s_cat_animation_timer != NULL) {
        lv_timer_delete(s_cat_animation_timer);
        s_cat_animation_timer = NULL;
    }
    if (s_cat_image != NULL) {
        lv_obj_delete(s_cat_image);
        s_cat_image = NULL;
    }
    s_cat_frame = NULL;
    s_cat_voice_state_valid = false;
    s_cat_disabled = true;
    ESP_LOGE(TAG, "LCD_CAT_DISABLED reason=allocation_failed");
}

static void lcd_cat_set_frame(const lv_image_dsc_t *frame, int16_t offset_x, int16_t offset_y)
{
    if (s_cat_image == NULL || frame == NULL) {
        return;
    }
    if (s_cat_frame != frame) {
        lv_image_set_src(s_cat_image, frame);
        s_cat_frame = frame;
    }
    lv_obj_set_pos(s_cat_image, LCD_CAT_IMAGE_X + offset_x, LCD_CAT_IMAGE_Y + offset_y);
}

static void lcd_cat_apply_static_state(lcd_dashboard_voice_state_t state)
{
    switch (state) {
    case LCD_DASHBOARD_VOICE_WAKE:
    case LCD_DASHBOARD_VOICE_WAIT:
        lcd_cat_set_frame(&s_cat_open, 0, 0);
        break;
    case LCD_DASHBOARD_VOICE_REC:
        lcd_cat_set_frame(&s_cat_rec, 0, 0);
        break;
    case LCD_DASHBOARD_VOICE_PLAY:
        lcd_cat_set_frame(&s_cat_open, 0, 0);
        break;
    case LCD_DASHBOARD_VOICE_ERR:
        lcd_cat_set_frame(&s_cat_error, 0, 0);
        break;
    case LCD_DASHBOARD_VOICE_LISTEN:
    default:
        lcd_cat_set_frame(&s_cat_listen, 0, 0);
        break;
    }
}

static bool lcd_cat_create(lv_obj_t *screen)
{
    s_cat_disabled = false;
    s_cat_image = lv_image_create(screen);
    if (s_cat_image == NULL) {
        lcd_cat_disable_after_allocation_failure();
        return false;
    }
    s_cat_frame = NULL;
    lcd_cat_set_frame(&s_cat_listen, 0, 0);

    s_cat_animation_timer = lv_timer_create(lcd_cat_animation_timer_cb,
                                             LCD_CAT_ANIMATION_PERIOD_MS,
                                             NULL);
    if (s_cat_animation_timer == NULL) {
        lcd_cat_disable_after_allocation_failure();
        return false;
    }
    lv_timer_pause(s_cat_animation_timer);
    return true;
}

static void lcd_cat_animation_timer_cb(lv_timer_t *timer)
{
    if (s_cat_disabled || timer == NULL) {
        if (timer != NULL) {
            lv_timer_pause(timer);
        }
        return;
    }
    if (s_cat_voice_state == LCD_DASHBOARD_VOICE_WAKE) {
        s_cat_animation_frame++;
        if (s_cat_animation_frame >= LCD_CAT_WAKE_FRAMES) {
            lcd_cat_set_frame(&s_cat_open, 0, 0);
            lv_timer_pause(timer);
            return;
        }
        const int16_t head_offset = s_cat_animation_frame < 3U ? -2 :
                                    (s_cat_animation_frame < 5U ? -1 : 0);
        lcd_cat_set_frame(&s_cat_open, 0, head_offset);
        return;
    }

    if (s_cat_voice_state == LCD_DASHBOARD_VOICE_REC) {
        s_cat_animation_frame++;
        if (s_cat_animation_frame >= LCD_CAT_RECORDING_FRAMES) {
            lcd_cat_set_frame(&s_cat_rec, 0, 0);
            lv_timer_pause(timer);
            return;
        }
        const int16_t paw_shift = (s_cat_animation_frame & 1U) != 0U ? 1 : 0;
        lcd_cat_set_frame(&s_cat_rec, paw_shift, 0);
        return;
    }

    if (s_cat_speaker_active) {
        s_cat_play_frame++;
        if (s_cat_play_frame >= LCD_CAT_PLAY_MOUTH_PERIOD_FRAMES) {
            s_cat_play_frame = 0;
            s_cat_play_mouth_open = !s_cat_play_mouth_open;
            lcd_cat_set_frame(s_cat_play_mouth_open ? &s_cat_play : &s_cat_open, 0, 0);
        }
        return;
    }

    lv_timer_pause(timer);
}

static void lcd_cat_update_for_voice_state(lcd_dashboard_voice_state_t state,
                                           bool speaker_active)
{
    if (s_cat_disabled || s_cat_image == NULL) {
        return;
    }

    if (!s_cat_voice_state_valid || s_cat_voice_state != state) {
        s_cat_voice_state = state;
        s_cat_voice_state_valid = true;
        s_cat_animation_frame = 0;
        s_cat_play_frame = 0;
        s_cat_play_mouth_open = false;
        lcd_cat_apply_static_state(state);
        if (s_cat_animation_timer == NULL) {
            return;
        }

        if (state == LCD_DASHBOARD_VOICE_WAKE) {
            lcd_cat_set_frame(&s_cat_open, 0, -2);
            lv_timer_resume(s_cat_animation_timer);
        } else if (state == LCD_DASHBOARD_VOICE_REC && !speaker_active) {
            lv_timer_resume(s_cat_animation_timer);
        } else {
            lv_timer_pause(s_cat_animation_timer);
        }
    }

    if (s_cat_speaker_active == speaker_active || s_cat_animation_timer == NULL) {
        return;
    }
    s_cat_speaker_active = speaker_active;
    s_cat_play_frame = 0;
    if (speaker_active) {
        s_cat_play_mouth_open = true;
        lcd_cat_set_frame(&s_cat_play, 0, 0);
        lv_timer_resume(s_cat_animation_timer);
        ESP_LOGI(TAG, "LCD_CAT_AUDIO_SYNC start");
    } else {
        lv_timer_pause(s_cat_animation_timer);
        if (s_cat_voice_state == LCD_DASHBOARD_VOICE_PLAY) {
            s_cat_play_mouth_open = false;
            lcd_cat_set_frame(&s_cat_open, 0, 0);
        }
        ESP_LOGI(TAG, "LCD_CAT_AUDIO_SYNC stop");
    }
}

static lv_obj_t *lcd_lvgl_create_root(lv_obj_t *screen, bool opaque)
{
    lv_obj_t *root = lv_obj_create(screen);
    if (root == NULL) {
        return NULL;
    }
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, LCD_H_RES, LCD_V_RES);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, opaque ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    return root;
}

static void lcd_voice_ui_disable(void)
{
    if (s_voice_animation_timer != NULL) {
        lv_timer_delete(s_voice_animation_timer);
        s_voice_animation_timer = NULL;
    }
    if (s_voice_root != NULL) {
        lv_obj_delete(s_voice_root);
        s_voice_root = NULL;
    }
    s_voice_cat_image = NULL;
    s_voice_cat_frame = NULL;
    s_voice_ui_visible = false;
    s_voice_ui_disabled = true;
    ESP_LOGE(TAG, "LCD_VOICE_UI_DISABLED reason=allocation_failed");
}

static void lcd_voice_ui_set_frame(const lv_image_dsc_t *frame, int16_t offset_x)
{
    if (s_voice_cat_image == NULL || frame == NULL) {
        return;
    }
    if (s_voice_cat_frame != frame) {
        lv_image_set_src(s_voice_cat_image, frame);
        s_voice_cat_frame = frame;
    }
    lv_obj_set_pos(s_voice_cat_image, LCD_VOICE_CAT_IMAGE_X + offset_x, LCD_VOICE_CAT_IMAGE_Y);
}

static void lcd_voice_ui_apply_state(lcd_dashboard_voice_state_t state)
{
    switch (state) {
    case LCD_DASHBOARD_VOICE_WAKE:
        lcd_voice_ui_set_frame(&s_cat_listen, 0);
        break;
    case LCD_DASHBOARD_VOICE_REC:
        lcd_voice_ui_set_frame(&s_cat_rec, 0);
        break;
    case LCD_DASHBOARD_VOICE_WAIT:
        lcd_voice_ui_set_frame(&s_cat_open, 0);
        break;
    case LCD_DASHBOARD_VOICE_PLAY:
        lcd_voice_ui_set_frame(&s_cat_open, 0);
        break;
    case LCD_DASHBOARD_VOICE_ERR:
        lcd_voice_ui_set_frame(&s_cat_error, 0);
        break;
    case LCD_DASHBOARD_VOICE_LISTEN:
    default:
        break;
    }
}

static bool lcd_voice_ui_create(lv_obj_t *screen)
{
    s_voice_ui_disabled = false;
    s_voice_root = lcd_lvgl_create_root(screen, true);
    if (s_voice_root == NULL) {
        lcd_voice_ui_disable();
        return false;
    }
    s_voice_cat_image = lv_image_create(s_voice_root);
    if (s_voice_cat_image == NULL) {
        lcd_voice_ui_disable();
        return false;
    }
    s_voice_cat_frame = NULL;
    lcd_voice_ui_set_frame(&s_cat_listen, 0);
    s_voice_animation_timer = lv_timer_create(lcd_voice_animation_timer_cb,
                                               LCD_CAT_ANIMATION_PERIOD_MS,
                                               NULL);
    if (s_voice_animation_timer == NULL) {
        lcd_voice_ui_disable();
        return false;
    }
    lv_timer_pause(s_voice_animation_timer);
    lv_obj_add_flag(s_voice_root, LV_OBJ_FLAG_HIDDEN);
    return true;
}

static void lcd_voice_animation_timer_cb(lv_timer_t *timer)
{
    if (s_voice_ui_disabled || !s_voice_ui_visible || timer == NULL) {
        if (timer != NULL) {
            lv_timer_pause(timer);
        }
        return;
    }
    if (s_voice_ui_state == LCD_DASHBOARD_VOICE_REC) {
        s_voice_animation_frame++;
        if (s_voice_animation_frame >= LCD_CAT_RECORDING_FRAMES) {
            lcd_voice_ui_set_frame(&s_cat_rec, 0);
            lv_timer_pause(timer);
            return;
        }
        lcd_voice_ui_set_frame(&s_cat_rec, (s_voice_animation_frame & 1U) != 0U ? 1 : 0);
        return;
    }
    if (s_voice_speaker_active) {
        s_voice_play_frame++;
        if (s_voice_play_frame >= LCD_CAT_PLAY_MOUTH_PERIOD_FRAMES) {
            s_voice_play_frame = 0;
            s_voice_play_mouth_open = !s_voice_play_mouth_open;
            lcd_voice_ui_set_frame(s_voice_play_mouth_open ? &s_cat_play : &s_cat_open, 0);
        }
        return;
    }
    lv_timer_pause(timer);
}

static void lcd_voice_ui_update(lcd_dashboard_voice_state_t state, bool speaker_active)
{
    const bool active = state != LCD_DASHBOARD_VOICE_LISTEN;
    if (s_voice_ui_disabled || s_voice_root == NULL || s_dashboard_root == NULL) {
        return;
    }
    if (!active) {
        if (s_voice_ui_visible) {
            lv_timer_pause(s_voice_animation_timer);
            lv_obj_add_flag(s_voice_root, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_dashboard_root, LV_OBJ_FLAG_HIDDEN);
            s_voice_ui_visible = false;
            ESP_LOGI(TAG, "LCD_UI_MODE voice->dashboard reason=voice_listen");
        }
        if (s_voice_speaker_active) {
            ESP_LOGI(TAG, "LCD_CAT_AUDIO_SYNC stop");
        }
        s_voice_ui_state = LCD_DASHBOARD_VOICE_LISTEN;
        s_voice_speaker_active = false;
        return;
    }

    if (!s_voice_ui_visible) {
        lv_timer_pause(s_cat_animation_timer);
        lv_obj_add_flag(s_dashboard_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_voice_root, LV_OBJ_FLAG_HIDDEN);
        s_voice_ui_visible = true;
        ESP_LOGI(TAG, "LCD_UI_MODE dashboard->voice generation=state_snapshot");
    }
    if (s_voice_ui_state != state) {
        s_voice_ui_state = state;
        s_voice_animation_frame = 0;
        s_voice_play_frame = 0;
        s_voice_play_mouth_open = false;
        lcd_voice_ui_apply_state(state);
        if (state == LCD_DASHBOARD_VOICE_REC && !speaker_active) {
            lv_timer_resume(s_voice_animation_timer);
        } else {
            lv_timer_pause(s_voice_animation_timer);
        }
    }

    if (s_voice_speaker_active == speaker_active) {
        return;
    }
    s_voice_speaker_active = speaker_active;
    s_voice_play_frame = 0;
    if (speaker_active) {
        s_voice_play_mouth_open = true;
        lcd_voice_ui_set_frame(&s_cat_play, 0);
        lv_timer_resume(s_voice_animation_timer);
        ESP_LOGI(TAG, "LCD_CAT_AUDIO_SYNC start");
    } else {
        lv_timer_pause(s_voice_animation_timer);
        if (s_voice_ui_state == LCD_DASHBOARD_VOICE_PLAY) {
            s_voice_play_mouth_open = false;
            lcd_voice_ui_set_frame(&s_cat_open, 0);
        }
        ESP_LOGI(TAG, "LCD_CAT_AUDIO_SYNC stop");
    }
}

static void lcd_lvgl_update_dashboard(const lcd_dashboard_snapshot_t *snapshot,
                                      bool update_dashboard_data)
{
    if (snapshot == NULL) {
        return;
    }

    lcd_voice_ui_update(snapshot->voice_state, snapshot->speaker_active);
    if (!s_voice_ui_visible) {
        lcd_cat_update_for_voice_state(snapshot->voice_state, snapshot->speaker_active);
    }
    if (!update_dashboard_data) {
        return;
    }

    if (snapshot->bme_valid) {
        (void)snprintf(s_temp_text, sizeof(s_temp_text),
                       "Temp      %.1f \xC2\xB0" "C", (double)snapshot->temperature_c);
        (void)snprintf(s_humidity_text, sizeof(s_humidity_text), "Humidity  %.1f %%", (double)snapshot->humidity_percent);
        (void)snprintf(s_pressure_text, sizeof(s_pressure_text), "Pressure  %.1f hPa", (double)snapshot->pressure_hpa);
        (void)snprintf(s_gas_text, sizeof(s_gas_text), "Gas       %.1f kOhm",
                       (double)snapshot->gas_resistance_ohm / 1000.0);
        (void)snprintf(s_air_text, sizeof(s_air_text), "Air       %s",
                       lcd_dashboard_air_state_name(snapshot->air_state));
    } else {
        (void)snprintf(s_temp_text, sizeof(s_temp_text), "Temp      --.- \xC2\xB0" "C");
        (void)snprintf(s_humidity_text, sizeof(s_humidity_text), "Humidity  --.- %%");
        (void)snprintf(s_pressure_text, sizeof(s_pressure_text), "Pressure  ----.- hPa");
        (void)snprintf(s_gas_text, sizeof(s_gas_text), "Gas       --.- kOhm");
        (void)snprintf(s_air_text, sizeof(s_air_text), "Air       INIT");
    }

    (void)snprintf(s_network_text, sizeof(s_network_text), "NET  %s   S3  %s",
                   snapshot->network_ok ? "OK" : "OFF",
                   snapshot->gateway_ok ? "OK" : "OFF");
    (void)snprintf(s_voice_text, sizeof(s_voice_text), "VOICE %s",
                   lcd_dashboard_voice_state_name(snapshot->voice_state));

    lcd_lvgl_set_static_text(s_temp_label, s_temp_text);
    lcd_lvgl_set_static_text(s_humidity_label, s_humidity_text);
    lcd_lvgl_set_static_text(s_pressure_label, s_pressure_text);
    lcd_lvgl_set_static_text(s_gas_label, s_gas_text);
    lcd_lvgl_set_static_text(s_air_label, s_air_text);
    lcd_lvgl_set_static_text(s_network_label, s_network_text);
    lcd_lvgl_set_static_text(s_voice_label, s_voice_text);
}

/* This callback runs in the LVGL task. The provider copies only already-published snapshots. */
static void lcd_lvgl_dashboard_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    lcd_dashboard_snapshot_provider_t provider = NULL;
    void *provider_ctx = NULL;
    portENTER_CRITICAL(&s_snapshot_provider_lock);
    provider = s_snapshot_provider;
    provider_ctx = s_snapshot_provider_ctx;
    portEXIT_CRITICAL(&s_snapshot_provider_lock);

    lcd_dashboard_snapshot_t snapshot = {
        .voice_state = LCD_DASHBOARD_VOICE_LISTEN,
    };
    if (provider != NULL) {
        (void)provider(&snapshot, provider_ctx);
    }
    const int64_t now_ms = esp_timer_get_time() / 1000;
    const bool update_dashboard_data = s_last_dashboard_data_update_ms == 0 ||
                                       now_ms - s_last_dashboard_data_update_ms >=
                                           LCD_DASHBOARD_DATA_REFRESH_MS;
    if (update_dashboard_data) {
        s_last_dashboard_data_update_ms = now_ms;
    }
    lcd_lvgl_update_dashboard(&snapshot, update_dashboard_data);

    if (s_last_dashboard_log_ms == 0 ||
        now_ms - s_last_dashboard_log_ms >= LCD_DASHBOARD_LOG_PERIOD_MS) {
        s_last_dashboard_log_ms = now_ms;
        ESP_LOGI(TAG,
                 "LCD_DATA_UPDATE bme_valid=%d csi_valid=%d net=%d voice=%s",
                 snapshot.bme_valid ? 1 : 0,
                 snapshot.csi_valid ? 1 : 0,
                 snapshot.network_ok ? 1 : 0,
                 lcd_dashboard_voice_state_name(snapshot.voice_state));
    }
}

static void lcd_lvgl_log_heap(const char *stage)
{
    ESP_LOGI(MEM_TAG,
             "stage=%s internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u",
             stage,
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}

static void lcd_lvgl_log_memory(const char *stage)
{
    lv_mem_monitor_t monitor;
    lv_mem_monitor(&monitor);

    ESP_LOGI(MEM_TAG,
             "stage=%s total_size=%u free_size=%u free_biggest_size=%u max_used=%u used_pct=%u frag_pct=%u "
             "internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u spiram_free=%u spiram_largest=%u",
             stage,
             (unsigned int)monitor.total_size,
             (unsigned int)monitor.free_size,
             (unsigned int)monitor.free_biggest_size,
             (unsigned int)monitor.max_used,
             (unsigned int)monitor.used_pct,
             (unsigned int)monitor.frag_pct,
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

/* LVGL invokes timer callbacks from lv_timer_handler() while the port lock is held. */
static void lcd_lvgl_memory_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    lcd_lvgl_log_memory("periodic");
}

static esp_err_t lcd_lvgl_create_status_ui(void)
{
    if (!lvgl_port_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }

    lv_obj_t *screen = lv_display_get_screen_active(s_display);
    /* A previous firmware/page may have left pixels in the panel framebuffer. Clean
     * the active LVGL screen before rebuilding the page so no retired objects remain. */
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    s_dashboard_root = lcd_lvgl_create_root(screen, false);
    if (s_dashboard_root == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_NO_MEM;
    }

    const lv_color_t title_color = lv_color_hex(0xFFFFFF);
    const lv_color_t section_color = lv_color_hex(0x79D2A6);
    const lv_color_t value_color = lv_color_hex(0xDCE7EF);
    lv_obj_t *title = lcd_lvgl_create_label(s_dashboard_root, "SensAir C5", title_color, 12);
    lv_obj_t *environment = lcd_lvgl_create_label(s_dashboard_root, "ENVIRONMENT", section_color, 40);
    s_temp_label = lcd_lvgl_create_label(s_dashboard_root, s_temp_text, value_color, 60);
    s_humidity_label = lcd_lvgl_create_label(s_dashboard_root, s_humidity_text, value_color, 82);
    s_pressure_label = lcd_lvgl_create_label(s_dashboard_root, s_pressure_text, value_color, 104);
    s_gas_label = lcd_lvgl_create_label(s_dashboard_root, s_gas_text, value_color, 126);
    s_air_label = lcd_lvgl_create_label(s_dashboard_root, s_air_text, section_color, 148);
    s_network_label = lcd_lvgl_create_label(s_dashboard_root, s_network_text, section_color, 238);
    s_voice_label = lcd_lvgl_create_label(s_dashboard_root, s_voice_text, title_color, 258);
    if (title == NULL || environment == NULL || s_temp_label == NULL || s_humidity_label == NULL ||
        s_pressure_label == NULL || s_gas_label == NULL || s_air_label == NULL ||
        s_network_label == NULL || s_voice_label == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_NO_MEM;
    }

    lcd_lvgl_log_memory("before_cat_create");
    (void)lcd_cat_create(s_dashboard_root);
    lcd_lvgl_log_memory("after_cat_create");
    lcd_lvgl_log_memory("before_voice_ui_create");
    (void)lcd_voice_ui_create(screen);
    lcd_lvgl_log_memory("after_voice_ui_create");

    lcd_dashboard_snapshot_t initial_snapshot = {
        .voice_state = LCD_DASHBOARD_VOICE_LISTEN,
    };
    lcd_lvgl_update_dashboard(&initial_snapshot, true);

    lcd_lvgl_log_memory("before_first_refresh");
    lcd_lvgl_log_memory("before_first_voice_refresh");
    /* The display intentionally keeps a one-line partial DMA buffer. Invalidate the
     * complete screen once after the layout is built, then render it synchronously so
     * the panel's old PRESENCE area is overwritten by the background immediately. */
    lv_obj_invalidate(screen);
    lv_refr_now(s_display);
    lcd_lvgl_log_memory("after_first_refresh");
    lcd_lvgl_log_memory("after_first_voice_refresh");

    if (lv_timer_create(lcd_lvgl_dashboard_timer_cb,
                        LCD_DASHBOARD_SNAPSHOT_REFRESH_MS,
                        NULL) == NULL) {
        ESP_LOGE(TAG, "dashboard timer creation failed");
        lvgl_port_unlock();
        return ESP_ERR_NO_MEM;
    }

    if (lv_timer_create(lcd_lvgl_memory_timer_cb, LCD_LVGL_MEM_LOG_PERIOD_MS, NULL) == NULL) {
        ESP_LOGE(MEM_TAG, "periodic timer creation failed");
        lvgl_port_unlock();
        return ESP_ERR_NO_MEM;
    }

    lcd_lvgl_log_memory("initial_ui_ready");
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t lcd_service_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    esp_err_t ret = lcd_init();
    if (ret != ESP_OK) {
        return ret;
    }

    lcd_lvgl_log_heap("legacy_buffer_release_before");
    ret = lcd_release_legacy_draw_buffer();
    if (ret != ESP_OK) {
        return ret;
    }
    lcd_lvgl_log_heap("legacy_buffer_release_after");

    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = LCD_LVGL_TASK_PRIORITY,
        .task_stack = LCD_LVGL_TASK_STACK,
        .task_affinity = -1,
        .task_max_sleep_ms = 1000,
        .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
        .timer_period_ms = LCD_LVGL_TIMER_PERIOD_MS,
    };
    lcd_lvgl_log_heap("lvgl_port_init_before");
    ret = lvgl_port_init(&lvgl_cfg);
    lcd_lvgl_log_heap("lvgl_port_init_after");
    if (ret != ESP_OK) {
        return ret;
    }

    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = lcd_get_io_handle(),
        .panel_handle = lcd_get_panel_handle(),
        .control_handle = NULL,
        .buffer_size = LCD_LVGL_DRAW_BUFFER_PIXELS,
        .double_buffer = false,
        .trans_size = 0,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = LCD_SWAP_XY_ENABLE,
            .mirror_x = LCD_MIRROR_X_ENABLE,
            .mirror_y = LCD_MIRROR_Y_ENABLE,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
            .swap_bytes = LCD_COLOR_SWAP_BYTES != 0,
            .full_refresh = false,
            .direct_mode = false,
        },
    };

    s_display = lvgl_port_add_disp(&display_cfg);
    if (s_display == NULL) {
        (void)lvgl_port_deinit();
        return ESP_ERR_NO_MEM;
    }
    lcd_lvgl_log_heap("lvgl_display_created");
    ESP_LOGI(TAG, "LVGL display created: buffer_pixels=%u buffer_bytes=%u",
             (unsigned int)LCD_LVGL_DRAW_BUFFER_PIXELS,
             (unsigned int)(LCD_LVGL_DRAW_BUFFER_PIXELS * sizeof(uint16_t)));

    ret = lcd_lvgl_create_status_ui();
    if (ret != ESP_OK) {
        (void)lvgl_port_remove_disp(s_display);
        s_display = NULL;
        (void)lvgl_port_deinit();
        return ret;
    }

    lcd_lvgl_log_heap("display_on_before");
    if (!lvgl_port_lock(1000)) {
        (void)lvgl_port_remove_disp(s_display);
        s_display = NULL;
        (void)lvgl_port_deinit();
        return ESP_ERR_TIMEOUT;
    }
    ret = lcd_set_display_enabled(true);
    lvgl_port_unlock();
    lcd_lvgl_log_heap("display_on_after");
    if (ret != ESP_OK) {
        (void)lcd_set_display_enabled(false);
        (void)lvgl_port_remove_disp(s_display);
        s_display = NULL;
        (void)lvgl_port_deinit();
        return ret;
    }

    s_started = true;
    ESP_LOGI(TAG, "started with %u-byte single DMA draw buffer",
             (unsigned int)(LCD_LVGL_DRAW_BUFFER_PIXELS * sizeof(uint16_t)));
    return ESP_OK;
}

bool lcd_service_is_started(void)
{
    return s_started;
}

void lcd_service_set_snapshot_provider(lcd_dashboard_snapshot_provider_t provider, void *user_ctx)
{
    portENTER_CRITICAL(&s_snapshot_provider_lock);
    s_snapshot_provider = provider;
    s_snapshot_provider_ctx = user_ctx;
    portEXIT_CRITICAL(&s_snapshot_provider_lock);
}
