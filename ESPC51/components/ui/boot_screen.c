#include "boot_screen.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "BOOT_SCREEN";

static lv_obj_t *boot_screen_root = NULL;
static lv_obj_t *s_boot_dot;
static lv_obj_t *s_boot_title;
static lv_obj_t *s_boot_status;
static lv_obj_t *s_boot_progress;
static lv_timer_t *s_boot_cleanup_timer;
static lv_timer_t *s_dashboard_refresh_timer;

/*
 * The boot UI is an overlay so the existing Dashboard and voice pages are
 * created exactly once and are revealed unchanged when this root is deleted.
 * The stage 2 title is the deliberate logo replacement point:
 * a future LVGL image can be added there without changing startup sequencing.
 */

/* Stage 1: fade in a fixed-size center dot during the first 500 ms. */
static void boot_screen_set_opa(void *obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, LV_PART_MAIN);
}

static void boot_screen_set_progress(void *obj, int32_t value)
{
    lv_bar_set_value((lv_obj_t *)obj, value, LV_ANIM_OFF);
}

static void boot_screen_show_object(void *obj, int32_t value)
{
    if (value != 0) {
        lv_obj_clear_flag((lv_obj_t *)obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void boot_screen_hide_object(void *obj, int32_t value)
{
    if (value != 0) {
        lv_obj_add_flag((lv_obj_t *)obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void boot_screen_schedule_visibility(lv_obj_t *obj, bool show, uint32_t delay_ms)
{
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, show ? boot_screen_show_object : boot_screen_hide_object);
    lv_anim_set_values(&anim, 0, 1);
    lv_anim_set_duration(&anim, 1);
    lv_anim_set_delay(&anim, delay_ms);
    lv_anim_start(&anim);
}

static void boot_screen_clear_object_refs(void)
{
    s_boot_dot = NULL;
    s_boot_title = NULL;
    s_boot_status = NULL;
    s_boot_progress = NULL;
}

static void boot_screen_stop_animations(void)
{
    if (s_boot_dot != NULL) {
        (void)lv_anim_delete(s_boot_dot, NULL);
    }
    if (s_boot_title != NULL) {
        (void)lv_anim_delete(s_boot_title, NULL);
    }
    if (s_boot_status != NULL) {
        (void)lv_anim_delete(s_boot_status, NULL);
    }
    if (s_boot_progress != NULL) {
        (void)lv_anim_delete(s_boot_progress, NULL);
    }
}

void boot_screen_set_dashboard_refresh_timer(lv_timer_t *timer)
{
    s_dashboard_refresh_timer = timer;
}

static void boot_screen_cleanup_timer_cb(lv_timer_t *timer)
{
    if (timer == s_boot_cleanup_timer) {
        s_boot_cleanup_timer = NULL;
    }

    /* This runs outside the progress animation completion callback. */
    boot_screen_stop_animations();
    if (boot_screen_root != NULL) {
        lv_obj_delete(boot_screen_root);
        boot_screen_root = NULL;
    }
    boot_screen_clear_object_refs();
    if (s_dashboard_refresh_timer != NULL) {
        lv_timer_resume(s_dashboard_refresh_timer);
    }
    ESP_LOGI(TAG, "boot screen finished");

    if (timer != NULL) {
        lv_timer_delete(timer);
    }
}

static void boot_screen_finish_cb(lv_anim_t *anim)
{
    (void)anim;
    if (boot_screen_root == NULL || s_boot_cleanup_timer != NULL) {
        return;
    }

    s_boot_cleanup_timer = lv_timer_create(boot_screen_cleanup_timer_cb, 350, NULL);
    if (s_boot_cleanup_timer == NULL) {
        ESP_LOGW(TAG, "boot screen cleanup timer allocation failed");
        return;
    }
    lv_timer_set_repeat_count(s_boot_cleanup_timer, 1);
    lv_timer_set_auto_delete(s_boot_cleanup_timer, false);
}

static lv_obj_t *boot_screen_create_label(lv_obj_t *parent,
                                          const char *text,
                                          int16_t x,
                                          int16_t y,
                                          int16_t width,
                                          lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text_static(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    return label;
}

void boot_screen_start(void)
{
    lv_display_t *display = lv_display_get_default();
    if (display == NULL || !lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "boot screen skipped: LVGL display unavailable");
        return;
    }

    if (boot_screen_root != NULL || s_boot_cleanup_timer != NULL) {
        lvgl_port_unlock();
        return;
    }

    if (s_dashboard_refresh_timer != NULL) {
        lv_timer_pause(s_dashboard_refresh_timer);
    }

    lv_obj_t *screen = lv_display_get_screen_active(display);
    boot_screen_root = lv_obj_create(screen);
    if (boot_screen_root == NULL) {
        if (s_dashboard_refresh_timer != NULL) {
            lv_timer_resume(s_dashboard_refresh_timer);
        }
        lvgl_port_unlock();
        ESP_LOGW(TAG, "boot screen skipped: root allocation failed");
        return;
    }
    lv_obj_remove_style_all(boot_screen_root);
    lv_obj_set_pos(boot_screen_root, 0, 0);
    lv_obj_set_size(boot_screen_root,
                    lv_display_get_horizontal_resolution(display),
                    lv_display_get_vertical_resolution(display));
    lv_obj_set_style_bg_color(boot_screen_root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(boot_screen_root, LV_OPA_COVER, LV_PART_MAIN);

    const lv_color_t accent = lv_color_hex(0x79D2A6);
    const lv_color_t text_color = lv_color_hex(0xDCE7EF);
    s_boot_dot = lv_obj_create(boot_screen_root);
    if (s_boot_dot != NULL) {
        lv_obj_remove_style_all(s_boot_dot);
        lv_obj_set_size(s_boot_dot, 24, 24);
        lv_obj_align(s_boot_dot, LV_ALIGN_CENTER, 0, -18);
        lv_obj_set_style_radius(s_boot_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_boot_dot, accent, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_boot_dot, LV_OPA_TRANSP, LV_PART_MAIN);

        lv_anim_t dot_opa;
        lv_anim_init(&dot_opa);
        lv_anim_set_var(&dot_opa, s_boot_dot);
        lv_anim_set_exec_cb(&dot_opa, boot_screen_set_opa);
        lv_anim_set_values(&dot_opa, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_duration(&dot_opa, 500);
        lv_anim_start(&dot_opa);
        boot_screen_schedule_visibility(s_boot_dot, false, 500);
    }

    /* Stage 2: title; an image can later replace this label as the product logo. */
    s_boot_title = boot_screen_create_label(boot_screen_root,
                                            "Sensair Shuttle",
                                            0,
                                            72,
                                            240,
                                            text_color);
    if (s_boot_title != NULL) {
        lv_obj_set_style_text_align(s_boot_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_add_flag(s_boot_title, LV_OBJ_FLAG_HIDDEN);
        boot_screen_schedule_visibility(s_boot_title, true, 500);
    }

    /* Stage 3: a compact status block; all children are owned by the boot root. */
    s_boot_status = lv_obj_create(boot_screen_root);
    if (s_boot_status != NULL) {
        lv_obj_remove_style_all(s_boot_status);
        lv_obj_set_pos(s_boot_status, 18, 142);
        lv_obj_set_size(s_boot_status, 204, 112);
        lv_obj_set_style_bg_opa(s_boot_status, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_add_flag(s_boot_status, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *heading = boot_screen_create_label(s_boot_status,
                                                      "System Starting...",
                                                      0,
                                                      0,
                                                      204,
                                                      text_color);
        if (heading != NULL) {
            lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        }
        (void)boot_screen_create_label(s_boot_status, "Display      OK", 18, 24, 170, text_color);
        (void)boot_screen_create_label(s_boot_status, "Sensor       INIT", 18, 43, 170, text_color);
        (void)boot_screen_create_label(s_boot_status, "Network      INIT", 18, 62, 170, text_color);
        (void)boot_screen_create_label(s_boot_status, "Audio        INIT", 18, 81, 170, text_color);

        s_boot_progress = lv_bar_create(s_boot_status);
        if (s_boot_progress != NULL) {
            lv_obj_set_pos(s_boot_progress, 18, 101);
            lv_obj_set_size(s_boot_progress, 168, 10);
            lv_obj_set_style_bg_color(s_boot_progress, lv_color_hex(0x26323A), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_boot_progress, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_boot_progress, accent, LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(s_boot_progress, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_set_style_radius(s_boot_progress, 3, LV_PART_MAIN);
            lv_obj_set_style_radius(s_boot_progress, 3, LV_PART_INDICATOR);
            lv_bar_set_range(s_boot_progress, 0, 100);
            lv_bar_set_value(s_boot_progress, 0, LV_ANIM_OFF);

            lv_anim_t progress;
            lv_anim_init(&progress);
            lv_anim_set_var(&progress, s_boot_progress);
            lv_anim_set_exec_cb(&progress, boot_screen_set_progress);
            lv_anim_set_values(&progress, 0, 100);
            lv_anim_set_duration(&progress, 1500);
            lv_anim_set_delay(&progress, 1500);
            lv_anim_set_completed_cb(&progress, boot_screen_finish_cb);
            lv_anim_start(&progress);
        }
        boot_screen_schedule_visibility(s_boot_status, true, 1500);
    }

    lv_obj_invalidate(boot_screen_root);
    lv_refr_now(display);
    ESP_LOGI(TAG, "boot screen started duration_ms=3000");
    lvgl_port_unlock();
}
