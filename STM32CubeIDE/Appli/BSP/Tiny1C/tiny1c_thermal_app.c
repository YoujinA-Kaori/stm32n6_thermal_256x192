#include "tiny1c_thermal_app.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "main.h"
#include "RGBLCD/rgblcd.h"
#include "thermal_project_config.h"

#define CFG_TINY1C_FRAME_WIDTH             CFG_THERMAL_SENSOR_WIDTH
#define CFG_TINY1C_FRAME_HEIGHT            CFG_THERMAL_SENSOR_HEIGHT
#define CFG_TINY1C_SCALE_FACTOR            CFG_THERMAL_NATIVE_PREVIEW_SCALE
#define CFG_TINY1C_BYTES_PER_PIXEL         2U
#define CFG_TINY1C_OUTPUT_IMAGE_AND_TEMP_ENABLE 1U
#define CFG_TINY1C_IMAGE_FRAME_WIDTH       CFG_TINY1C_FRAME_WIDTH
#define CFG_TINY1C_IMAGE_FRAME_HEIGHT      CFG_TINY1C_FRAME_HEIGHT
#define CFG_TINY1C_TEMP_FRAME_WIDTH        CFG_TINY1C_FRAME_WIDTH
#define CFG_TINY1C_TEMP_FRAME_HEIGHT       CFG_TINY1C_FRAME_HEIGHT
#define CFG_TINY1C_COMBINED_FRAME_HEIGHT   (CFG_TINY1C_IMAGE_FRAME_HEIGHT + CFG_TINY1C_TEMP_FRAME_HEIGHT)
#if (CFG_TINY1C_OUTPUT_IMAGE_AND_TEMP_ENABLE == 1U)
#define CFG_TINY1C_RAW_FRAME_HEIGHT        CFG_TINY1C_COMBINED_FRAME_HEIGHT
#else
#define CFG_TINY1C_RAW_FRAME_HEIGHT        CFG_TINY1C_TEMP_FRAME_HEIGHT
#endif
#define CFG_TINY1C_FRAME_BYTES             (CFG_TINY1C_IMAGE_FRAME_WIDTH * CFG_TINY1C_RAW_FRAME_HEIGHT * CFG_TINY1C_BYTES_PER_PIXEL)
#define CFG_TINY1C_TEMP_FRAME_BYTES        (CFG_TINY1C_TEMP_FRAME_WIDTH * CFG_TINY1C_TEMP_FRAME_HEIGHT * CFG_TINY1C_BYTES_PER_PIXEL)
#define CFG_TINY1C_DISP_WIDTH              (CFG_TINY1C_FRAME_WIDTH * CFG_TINY1C_SCALE_FACTOR)
#define CFG_TINY1C_DISP_HEIGHT             (CFG_TINY1C_FRAME_HEIGHT * CFG_TINY1C_SCALE_FACTOR)
#define CFG_TINY1C_PREVIEW_FPS             25U
#define CFG_TINY1C_PREVIEW_MODE_DVP        0U
#define CFG_TINY1C_TEMP_RIGHT_SHIFT        2U
#define CFG_TINY1C_TEMP14_MAX              16383U
#define CFG_TINY1C_TEMP_WORD_LITTLE_ENDIAN 1U
#define CFG_TINY1C_OVERLAY_FONT_SIZE       16U
#define CFG_TINY1C_OVERLAY_MARGIN_X        6U
#define CFG_TINY1C_OVERLAY_MARGIN_Y        6U
#define CFG_TINY1C_OVERLAY_BOX_WIDTH       150U
#define CFG_TINY1C_OVERLAY_BOX_HEIGHT      20U
#define CFG_TINY1C_CENTER_CROSS_HALF_SIZE  5U
#define CFG_TINY1C_GUI_MODE_ENABLE         1U
#define CFG_TINY1C_DEFAULT_DISTANCE_CM     50U
#define CFG_TINY1C_DEFAULT_EMISSIVITY_PCT  95U
#define CFG_TINY1C_DEFAULT_ATMOS_TEMP_C    25
#define CFG_TINY1C_DEFAULT_REFLECT_TEMP_C  25
#define CFG_TINY1C_DEFAULT_TAU_CNT_128     128U
#define CFG_TINY1C_DEFAULT_HIGH_GAIN       1U
#define CFG_TINY1C_AUTO_SHUTTER_ENABLE     1U
#define CFG_TINY1C_AUTO_SHUTTER_MIN_S      30U
#define CFG_TINY1C_AUTO_SHUTTER_MAX_S      180U
#define CFG_TINY1C_AUTO_SHUTTER_THRESH_CNT 72U
#define CFG_TINY1C_DEFAULT_PSEUDO_COLOR_MODE PSEUDO_COLOR_MODE_5
#define CFG_TINY1C_PREVIEW_CONTRAST_DEFAULT_SLIDER 54U
#define CFG_TINY1C_PREVIEW_MIRROR_DEFAULT_ENABLE  1U
#define CFG_TINY1C_PREVIEW_FLIP_DEFAULT_ENABLE    1U

static volatile uint8_t g_tiny1c_frame_ready = 0U;
static uint8_t g_tiny1c_raw_frame[CFG_TINY1C_FRAME_BYTES]
__attribute__((section(".EXTRAM"), aligned(32)));
static uint16_t g_tiny1c_temp14_frame[CFG_TINY1C_FRAME_WIDTH * CFG_TINY1C_FRAME_HEIGHT]
__attribute__((section(".EXTRAM"), aligned(32)));
static uint16_t g_tiny1c_rgb565_frame[CFG_TINY1C_DISP_WIDTH * CFG_TINY1C_DISP_HEIGHT]
__attribute__((section(".EXTRAM"), aligned(32)));
static uint16_t g_tiny1c_disp_x0 = 0U;
static uint16_t g_tiny1c_disp_y0 = 0U;
static volatile uint32_t g_tiny1c_frame_counter = 0U;
static volatile uint8_t g_tiny1c_center_temp_centi_c_valid = 0U;
static volatile int32_t g_tiny1c_center_temp_centi_c = 0;
static volatile uint8_t g_tiny1c_preview_mirror_enable = CFG_TINY1C_PREVIEW_MIRROR_DEFAULT_ENABLE;
static volatile uint8_t g_tiny1c_preview_flip_enable = CFG_TINY1C_PREVIEW_FLIP_DEFAULT_ENABLE;
static volatile uint8_t g_tiny1c_preview_contrast_slider = CFG_TINY1C_PREVIEW_CONTRAST_DEFAULT_SLIDER;

/**
 * @brief Convert one temp14 value to centi-degrees Celsius.
 * @param temp14_value Raw temperature value in 1/16 K units.
 * @return int32_t Converted temperature in 0.01 degrees Celsius.
 */
static int32_t tiny1c_thermal_app_temp14_to_centi_celsius(uint16_t temp14_value)
{
    int32_t scaled_temp;

    scaled_temp = ((int32_t)temp14_value * 100 + 8) / 16;
    scaled_temp -= 27315;

    return scaled_temp;
}

/**
 * @brief Unpack one raw Y14 frame into plain temp14 values.
 * @param source_frame Pointer to the raw temperature frame.
 * @param dest_frame Destination temp14 frame buffer.
 * @return None
 */
static void tiny1c_thermal_app_unpack_temp14_frame(const uint8_t *source_frame, uint16_t *dest_frame)
{
    const uint16_t *source_word_frame;
    uint32_t pixel_index;
    uint32_t pixel_count;

    if ((source_frame == NULL) || (dest_frame == NULL))
    {
        return;
    }

    source_word_frame = (const uint16_t *)(const void *)source_frame;
    pixel_count = (uint32_t)CFG_TINY1C_FRAME_WIDTH * (uint32_t)CFG_TINY1C_FRAME_HEIGHT;
    for (pixel_index = 0U; pixel_index < pixel_count; pixel_index++)
    {
        dest_frame[pixel_index] = (uint16_t)(source_word_frame[pixel_index] >> CFG_TINY1C_TEMP_RIGHT_SHIFT);
    }
}

static uint16_t tiny1c_thermal_app_rgb888_to_rgb565(uint8_t red8, uint8_t green8, uint8_t blue8);

/**
 * @brief Apply GUI contrast to one 8-bit color channel.
 * @param channel8 Input 8-bit channel value.
 * @return uint8_t Contrast-adjusted 8-bit channel value.
 */
static uint8_t tiny1c_thermal_app_apply_preview_contrast_u8(uint8_t channel8)
{
    int32_t contrast_percent;
    int32_t adjusted_value;

    contrast_percent = 50 + (int32_t)g_tiny1c_preview_contrast_slider;
    adjusted_value = (((int32_t)channel8 - 128) * contrast_percent) / 100 + 128;

    if (adjusted_value < 0)
    {
        return 0U;
    }
    if (adjusted_value > 255)
    {
        return 255U;
    }

    return (uint8_t)adjusted_value;
}

/**
 * @brief Store one 2x-scaled RGB565 pixel block into the preview buffer with transform applied.
 * @param source_x Source image X coordinate.
 * @param source_y Source image Y coordinate.
 * @param rgb565_pixel RGB565 pixel value.
 * @return None
 */
static void tiny1c_thermal_app_store_rgb565_pixel_2x(uint32_t source_x, uint32_t source_y, uint16_t rgb565_pixel)
{
    uint32_t dest_x;
    uint32_t dest_y;
    uint32_t row0_index;
    uint32_t row1_index;

    dest_x = source_x * CFG_TINY1C_SCALE_FACTOR;
    dest_y = source_y * CFG_TINY1C_SCALE_FACTOR;

    if (g_tiny1c_preview_mirror_enable != 0U)
    {
        dest_x = (uint32_t)CFG_TINY1C_DISP_WIDTH - CFG_TINY1C_SCALE_FACTOR - dest_x;
    }

    if (g_tiny1c_preview_flip_enable != 0U)
    {
        dest_y = (uint32_t)CFG_TINY1C_DISP_HEIGHT - CFG_TINY1C_SCALE_FACTOR - dest_y;
    }

    row0_index = dest_y * CFG_TINY1C_DISP_WIDTH + dest_x;
    row1_index = row0_index + CFG_TINY1C_DISP_WIDTH;

    g_tiny1c_rgb565_frame[row0_index] = rgb565_pixel;
    g_tiny1c_rgb565_frame[row0_index + 1U] = rgb565_pixel;
    g_tiny1c_rgb565_frame[row1_index] = rgb565_pixel;
    g_tiny1c_rgb565_frame[row1_index + 1U] = rgb565_pixel;
}

/**
 * @brief Get the center temperature value used by the overlay.
 * @param center_temp14 Raw center temperature value in temp14 format.
 * @return int32_t Center temperature in 0.01 degrees Celsius.
 */
static int32_t tiny1c_thermal_app_get_center_overlay_centi_celsius(uint16_t center_temp14)
{
    if (g_tiny1c_center_temp_centi_c_valid != 0U)
    {
        return g_tiny1c_center_temp_centi_c;
    }

    return tiny1c_thermal_app_temp14_to_centi_celsius(center_temp14);
}

/**
 * @brief Build a light soft-knee curve for display normalization.
 * @param None
 * @return None
 */
static void __attribute__((unused)) tiny1c_thermal_app_build_display_curve_lut(void)
{
}

/**
 * @brief Apply the display normalization curve.
 * @param normalized Normalized display value.
 * @return uint8_t Curve-mapped value.
 */
static uint8_t __attribute__((unused)) tiny1c_thermal_app_apply_display_curve(uint8_t normalized)
{
    return normalized;
}

/**
 * @brief Smooth a bounded uint16_t target toward a current value.
 * @param current Current value.
 * @param target Target value.
 * @param hysteresis Dead-band around the current value.
 * @param smooth_shift Right shift used to derive the step.
 * @param max_step Maximum step size.
 * @return uint16_t Smoothed value.
 */
static uint16_t __attribute__((unused)) tiny1c_thermal_app_smooth_u16(uint16_t current,
                                              uint16_t target,
                                              uint16_t hysteresis,
                                              uint8_t smooth_shift,
                                              uint16_t max_step)
{
    (void)target;
    (void)hysteresis;
    (void)smooth_shift;
    (void)max_step;
    return current;
}

/**
 * @brief Convert a display center/span pair back into a min/max range.
 * @param center Center point in temp14 units.
 * @param span Display span in temp14 units.
 * @param temp14_min Output minimum.
 * @param temp14_max Output maximum.
 * @return None
 */
static void __attribute__((unused)) tiny1c_thermal_app_center_span_to_range(uint16_t center,
                                                    uint16_t span,
                                                    uint16_t *temp14_min,
                                                    uint16_t *temp14_max)
{
    if ((temp14_min == NULL) || (temp14_max == NULL))
    {
        return;
    }
    *temp14_min = center;
    *temp14_max = (uint16_t)(center + span);
}

/**
 * @brief Convert distance in centimeters to Tiny1-C 128-count-per-meter units.
 * @param distance_cm Distance in centimeters.
 * @return uint16_t Distance parameter for TPD_PROP_DISTANCE.
 */
static uint16_t tiny1c_thermal_app_distance_cm_to_cnt_128(uint16_t distance_cm)
{
    uint32_t distance_cnt = (((uint32_t)distance_cm * 128U) + 50U) / 100U;

    if (distance_cnt > 25600U)
    {
        distance_cnt = 25600U;
    }

    return (uint16_t)distance_cnt;
}

/**
 * @brief Convert emissivity percentage to Tiny1-C 1/128 units.
 * @param emissivity_pct Emissivity percentage in the range [0, 100].
 * @return uint16_t Emissivity parameter for TPD_PROP_EMS.
 */
static uint16_t tiny1c_thermal_app_emissivity_pct_to_cnt_128(uint8_t emissivity_pct)
{
    uint32_t emissivity_cnt;

    if (emissivity_pct > 100U)
    {
        emissivity_pct = 100U;
    }

    emissivity_cnt = (((uint32_t)emissivity_pct * 128U) + 50U) / 100U;
    if (emissivity_cnt == 0U)
    {
        emissivity_cnt = 1U;
    }

    return (uint16_t)emissivity_cnt;
}

/**
 * @brief Configure Tiny1-C temperature, gain, and shutter defaults for stable radiometric startup.
 * @param None
 * @return ir_error_t `IR_SUCCESS` indicates all defaults were applied.
 */
static ir_error_t tiny1c_thermal_app_config_module_defaults(void)
{
    ir_error_t rst;

    rst = tiny1c_vdcmd_set_gain_mode(CFG_TINY1C_DEFAULT_HIGH_GAIN);
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = tiny1c_vdcmd_set_distance(tiny1c_thermal_app_distance_cm_to_cnt_128(CFG_TINY1C_DEFAULT_DISTANCE_CM));
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = tiny1c_vdcmd_set_emissivity(tiny1c_thermal_app_emissivity_pct_to_cnt_128(CFG_TINY1C_DEFAULT_EMISSIVITY_PCT));
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = tiny1c_vdcmd_set_atmospheric_temp_c(CFG_TINY1C_DEFAULT_ATMOS_TEMP_C);
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = tiny1c_vdcmd_set_reflected_temp_c(CFG_TINY1C_DEFAULT_REFLECT_TEMP_C);
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = tiny1c_vdcmd_set_tau(CFG_TINY1C_DEFAULT_TAU_CNT_128);
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = tiny1c_vdcmd_set_auto_shutter_enabled(CFG_TINY1C_AUTO_SHUTTER_ENABLE);
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = tiny1c_vdcmd_set_auto_shutter_min_interval(CFG_TINY1C_AUTO_SHUTTER_MIN_S);
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = tiny1c_vdcmd_set_auto_shutter_max_interval(CFG_TINY1C_AUTO_SHUTTER_MAX_S);
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = tiny1c_vdcmd_set_auto_shutter_temp_threshold(CFG_TINY1C_AUTO_SHUTTER_THRESH_CNT);
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    return IR_SUCCESS;
}

/**
 * @brief Clean one LCD framebuffer rectangle from D-Cache after CPU drawing.
 * @param x_start Rectangle start X coordinate.
 * @param y_start Rectangle start Y coordinate.
 * @param width Rectangle width in pixels.
 * @param height Rectangle height in pixels.
 * @return None
 */
static void tiny1c_thermal_app_clean_lcd_rect(uint16_t x_start, uint16_t y_start, uint16_t width, uint16_t height)
{
    uint16_t row_index;

    if ((width == 0U) || (height == 0U))
    {
        return;
    }

    for (row_index = 0U; row_index < height; row_index++)
    {
        uint16_t *row_addr = &g_ltdc_lcd_framebuf[(uint32_t)(y_start + row_index) * rgblcddev.pwidth + x_start];
        uintptr_t start_addr = ((uintptr_t)row_addr) & ~((uintptr_t)31U);
        uintptr_t end_addr = (((uintptr_t)row_addr + (uintptr_t)(width * sizeof(uint16_t)) + (uintptr_t)31U) & ~((uintptr_t)31U));

        SCB_CleanDCache_by_Addr((uint32_t *)start_addr, (int32_t)(end_addr - start_addr));
    }
}

/**
 * @brief Draw the center temperature overlay on the LCD.
 * @param center_temp14 Center-point temperature value in temp14 format.
 * @return None
 */
static void __attribute__((unused)) tiny1c_thermal_app_draw_center_temperature_overlay(uint16_t center_temp14)
{
    char center_temp_text[24];
    int32_t center_temp_centi = tiny1c_thermal_app_get_center_overlay_centi_celsius(center_temp14);
    int32_t center_temp_abs_centi;
    int32_t center_temp_int;
    int32_t center_temp_frac;
    uint16_t label_x = (uint16_t)(g_tiny1c_disp_x0 + CFG_TINY1C_OVERLAY_MARGIN_X);
    uint16_t label_y = (uint16_t)(g_tiny1c_disp_y0 + CFG_TINY1C_OVERLAY_MARGIN_Y);
    uint16_t center_x = (uint16_t)(g_tiny1c_disp_x0 + (CFG_TINY1C_DISP_WIDTH / 2U));
    uint16_t center_y = (uint16_t)(g_tiny1c_disp_y0 + (CFG_TINY1C_DISP_HEIGHT / 2U));
    uint16_t text_x;
    uint16_t text_width;
    uint8_t text_index;

    center_temp_abs_centi = (center_temp_centi < 0) ? -center_temp_centi : center_temp_centi;
    center_temp_int = center_temp_abs_centi / 100;
    center_temp_frac = center_temp_abs_centi % 100;

    (void)snprintf(center_temp_text,
                   sizeof(center_temp_text),
                   "Center:%s%" PRId32 ".%02" PRId32 "C",
                   (center_temp_centi < 0) ? "-" : "",
                   center_temp_int,
                   center_temp_frac);

    text_x = label_x;
    text_width = (uint16_t)(strlen(center_temp_text) * (CFG_TINY1C_OVERLAY_FONT_SIZE >> 1U));
    for (text_index = 0U; center_temp_text[text_index] != '\0'; text_index++)
    {
        rgblcd_show_char(text_x,
                         label_y,
                         center_temp_text[text_index],
                         CFG_TINY1C_OVERLAY_FONT_SIZE,
                         1U,
                         YELLOW);
        text_x = (uint16_t)(text_x + (CFG_TINY1C_OVERLAY_FONT_SIZE >> 1U));
    }

    rgblcd_draw_hline((uint16_t)(center_x - CFG_TINY1C_CENTER_CROSS_HALF_SIZE),
                      center_y,
                      (uint16_t)(CFG_TINY1C_CENTER_CROSS_HALF_SIZE * 2U + 1U),
                      WHITE);
    rgblcd_draw_line(center_x,
                     (uint16_t)(center_y - CFG_TINY1C_CENTER_CROSS_HALF_SIZE),
                     center_x,
                     (uint16_t)(center_y + CFG_TINY1C_CENTER_CROSS_HALF_SIZE),
                     WHITE);

    tiny1c_thermal_app_clean_lcd_rect(label_x,
                                      label_y,
                                      (text_width == 0U) ? 1U : text_width,
                                      CFG_TINY1C_OVERLAY_BOX_HEIGHT);
    tiny1c_thermal_app_clean_lcd_rect((uint16_t)(center_x - CFG_TINY1C_CENTER_CROSS_HALF_SIZE),
                                      (uint16_t)(center_y - CFG_TINY1C_CENTER_CROSS_HALF_SIZE),
                                      (uint16_t)(CFG_TINY1C_CENTER_CROSS_HALF_SIZE * 2U + 1U),
                                      (uint16_t)(CFG_TINY1C_CENTER_CROSS_HALF_SIZE * 2U + 1U));
}

/**
 * @brief Override the center temperature overlay value.
 * @param center_temp_centi_c Compensated center temperature in centi-degrees Celsius.
 * @return None
 */
void tiny1c_thermal_app_set_center_temp_centi_c(int32_t center_temp_centi_c)
{
    g_tiny1c_center_temp_centi_c = center_temp_centi_c;
    g_tiny1c_center_temp_centi_c_valid = 1U;
}

/**
 * @brief 灏?8bit RGB888 閫氶亾鍊煎帇缂╀负 RGB565銆? * @param red8 绾㈣壊閫氶亾鍊硷紝鑼冨洿 0~255銆? * @param green8 缁胯壊閫氶亾鍊硷紝鑼冨洿 0~255銆? * @param blue8 钃濊壊閫氶亾鍊硷紝鑼冨洿 0~255銆? * @return uint16_t锛歊GB565 鍍忕礌鍊笺€? */
/**
 * @brief Convert one YUV pixel to RGB565.
 * @param y8 Luma component.
 * @param u8 Chroma U component.
 * @param v8 Chroma V component.
 * @return uint16_t RGB565 pixel value.
 */
static uint16_t tiny1c_thermal_app_yuv_to_rgb565(uint8_t y8, uint8_t u8, uint8_t v8)
{
    int32_t c;
    int32_t d;
    int32_t e;
    int32_t red32;
    int32_t green32;
    int32_t blue32;
    uint8_t red8;
    uint8_t green8;
    uint8_t blue8;

    c = (int32_t)y8 - 16;
    d = (int32_t)u8 - 128;
    e = (int32_t)v8 - 128;

    red32 = (298 * c + 409 * e + 128) >> 8;
    green32 = (298 * c - 100 * d - 208 * e + 128) >> 8;
    blue32 = (298 * c + 516 * d + 128) >> 8;

    if (red32 < 0)
    {
        red8 = 0U;
    }
    else if (red32 > 255)
    {
        red8 = 255U;
    }
    else
    {
        red8 = (uint8_t)red32;
    }

    if (green32 < 0)
    {
        green8 = 0U;
    }
    else if (green32 > 255)
    {
        green8 = 255U;
    }
    else
    {
        green8 = (uint8_t)green32;
    }

    if (blue32 < 0)
    {
        blue8 = 0U;
    }
    else if (blue32 > 255)
    {
        blue8 = 255U;
    }
    else
    {
        blue8 = (uint8_t)blue32;
    }

    red8 = tiny1c_thermal_app_apply_preview_contrast_u8(red8);
    green8 = tiny1c_thermal_app_apply_preview_contrast_u8(green8);
    blue8 = tiny1c_thermal_app_apply_preview_contrast_u8(blue8);

    return tiny1c_thermal_app_rgb888_to_rgb565(red8, green8, blue8);
}

/**
 * @brief Render one YUV422 image frame to the LCD buffer with 2x scale.
 * @param image_raw_frame Pointer to the YUV422 image plane.
 * @return None
 */
static void tiny1c_thermal_app_render_yuv422_image_frame_2x(const uint8_t *image_raw_frame)
{
    uint32_t y_index;
    uint32_t x_index;

    if (image_raw_frame == NULL)
    {
        return;
    }

    for (y_index = 0U; y_index < CFG_TINY1C_IMAGE_FRAME_HEIGHT; y_index++)
    {
        const uint8_t *src_line = &image_raw_frame[y_index * CFG_TINY1C_IMAGE_FRAME_WIDTH * CFG_TINY1C_BYTES_PER_PIXEL];

        for (x_index = 0U; x_index < CFG_TINY1C_IMAGE_FRAME_WIDTH; x_index += 2U)
        {
            uint32_t src_index = x_index * CFG_TINY1C_BYTES_PER_PIXEL;
            uint16_t rgb565_pixel0;
            uint16_t rgb565_pixel1;

            rgb565_pixel0 = tiny1c_thermal_app_yuv_to_rgb565(src_line[src_index],
                                                             src_line[src_index + 1U],
                                                             src_line[src_index + 3U]);
            rgb565_pixel1 = tiny1c_thermal_app_yuv_to_rgb565(src_line[src_index + 2U],
                                                             src_line[src_index + 1U],
                                                             src_line[src_index + 3U]);

            tiny1c_thermal_app_store_rgb565_pixel_2x(x_index, y_index, rgb565_pixel0);
            tiny1c_thermal_app_store_rgb565_pixel_2x(x_index + 1U, y_index, rgb565_pixel1);
        }
    }

    SCB_CleanDCache_by_Addr((uint32_t *)g_tiny1c_rgb565_frame,
                            (int32_t)(CFG_TINY1C_DISP_WIDTH * CFG_TINY1C_DISP_HEIGHT * sizeof(uint16_t)));

#if (CFG_TINY1C_GUI_MODE_ENABLE == 0U)
    rgblcd_color_fill(g_tiny1c_disp_x0,
                      g_tiny1c_disp_y0,
                      (uint16_t)(g_tiny1c_disp_x0 + CFG_TINY1C_DISP_WIDTH - 1U),
                      (uint16_t)(g_tiny1c_disp_y0 + CFG_TINY1C_DISP_HEIGHT - 1U),
                      g_tiny1c_rgb565_frame);
#endif
}

static uint16_t tiny1c_thermal_app_rgb888_to_rgb565(uint8_t red8, uint8_t green8, uint8_t blue8)
{
    return (uint16_t)(((uint16_t)(red8 & 0xF8U) << 8) | ((uint16_t)(green8 & 0xFCU) << 3) | ((uint16_t)blue8 >> 3));
}

/**
 * @brief 鍒濆鍖?LCD 鏄剧ず鍖哄煙锛屼娇鐑垚鍍忓浘灞呬腑鏄剧ず銆? * @param 鏃犮€? * @return 鏃犮€? */
static void tiny1c_thermal_app_display_region_init(void)
{
    if (rgblcddev.width > CFG_TINY1C_DISP_WIDTH)
    {
        g_tiny1c_disp_x0 = (uint16_t)((rgblcddev.width - CFG_TINY1C_DISP_WIDTH) / 2U);
    }
    else
    {
        g_tiny1c_disp_x0 = 0U;
    }

    if (rgblcddev.height > CFG_TINY1C_DISP_HEIGHT)
    {
        g_tiny1c_disp_y0 = (uint16_t)((rgblcddev.height - CFG_TINY1C_DISP_HEIGHT) / 2U);
    }
    else
    {
        g_tiny1c_disp_y0 = 0U;
    }
}

/**
 * @brief 灏?Y14 娓╁害鍊兼槧灏勪负 RGB565 浼僵鍍忕礌銆? * @param temp14_value 褰撳墠鍍忕礌娓╁害鍊硷紝14bit銆? * @param temp14_min 褰撳墠鏄剧ず鑼冨洿涓嬮檺銆? * @param temp14_max 褰撳墠鏄剧ず鑼冨洿涓婇檺銆? * @return uint16_t锛歊GB565 浼僵鍍忕礌鍊笺€? */
static uint16_t __attribute__((unused)) tiny1c_thermal_app_temp14_to_rgb565(uint16_t temp14_value, uint16_t temp14_min, uint16_t temp14_max)
{
    (void)temp14_value;
    (void)temp14_min;
    (void)temp14_max;
    return 0U;
}

/**
 * @brief 鍩轰簬鐩搁偦 4 涓俯搴︾偣鐢熸垚 2x 鏀惧ぇ鍧楀唴鐨勬彃鍊兼俯搴﹀€笺€? * @param temp00 宸︿笂瑙掓俯搴﹀€笺€? * @param temp10 鍙充笂瑙掓俯搴﹀€笺€? * @param temp01 宸︿笅瑙掓俯搴﹀€笺€? * @param temp11 鍙充笅瑙掓俯搴﹀€笺€? * @param sub_x 鐩爣鐐瑰湪 2x 鍧楀唴鐨?x 鍋忕Щ锛屽彧鍏佽 0 鎴?1銆? * @param sub_y 鐩爣鐐瑰湪 2x 鍧楀唴鐨?y 鍋忕Щ锛屽彧鍏佽 0 鎴?1銆? * @return uint16_t 鎻掑€煎悗鐨勬俯搴﹀€笺€? */
static uint16_t __attribute__((unused)) tiny1c_thermal_app_interp_temp14_2x(uint16_t temp00,
                                                    uint16_t temp10,
                                                    uint16_t temp01,
                                                    uint16_t temp11,
                                                    uint8_t sub_x,
                                                    uint8_t sub_y)
{
    uint32_t interp_value;

    if ((sub_x == 0U) && (sub_y == 0U))
    {
        return temp00;
    }
    if ((sub_x == 1U) && (sub_y == 0U))
    {
        return (uint16_t)(((uint32_t)temp00 + (uint32_t)temp10) / 2U);
    }
    if ((sub_x == 0U) && (sub_y == 1U))
    {
        return (uint16_t)(((uint32_t)temp00 + (uint32_t)temp01) / 2U);
    }

    interp_value = (uint32_t)temp00 + (uint32_t)temp10 + (uint32_t)temp01 + (uint32_t)temp11;
    return (uint16_t)(interp_value / 4U);
}

/**
 * @brief 淇濊瘉鏄剧ず鑼冨洿璺ㄥ害涓嶅皬浜庢渶灏忓€硷紝閬垮厤灏忔尝鍔ㄨ杩囧害鏀惧ぇ銆? * @param temp14_min 鏄剧ず鑼冨洿涓嬮檺鎸囬拡銆? * @param temp14_max 鏄剧ず鑼冨洿涓婇檺鎸囬拡銆? * @return 鏃犮€? */
static void __attribute__((unused)) tiny1c_thermal_app_enforce_min_span(uint16_t *temp14_min, uint16_t *temp14_max)
{
    if ((temp14_min == NULL) || (temp14_max == NULL))
    {
        return;
    }
    if (*temp14_max < *temp14_min)
    {
        *temp14_max = *temp14_min;
    }
}

/**
 * @brief 瀵圭洰鏍囨樉绀鸿竟鐣屽仛鎱㈤€熻窡韪紝鍑忚交 FFC 鍚庣殑绐佸彉瑙傛劅銆? * @param current 褰撳墠杈圭晫鍊笺€? * @param target 鐩爣杈圭晫鍊笺€? * @return uint16_t 骞虫粦鍚庣殑杈圭晫鍊笺€? */
static uint16_t __attribute__((unused)) tiny1c_thermal_app_smooth_range_value(uint16_t current, uint16_t target)
{
    (void)target;
    return current;
}

/**
 * @brief 鏍规嵁鏁村抚娓╁害鍒嗗竷璁＄畻涓讳綋鏄剧ず鍖洪棿锛屽拷鐣ュ皯閲忔瀬绔喎鐑儚绱犮€? * @param frame_temp14 鎸囧悜娓╁害甯х紦鍐插尯銆? * @param frame_temp14_min 褰撳墠甯ф渶灏忔俯搴﹀€笺€? * @param frame_temp14_max 褰撳墠甯ф渶澶ф俯搴﹀€笺€? * @param disp_temp14_min 杈撳嚭鏄剧ず涓嬮檺銆? * @param disp_temp14_max 杈撳嚭鏄剧ず涓婇檺銆? * @return 鏃犮€? */
static void __attribute__((unused)) tiny1c_thermal_app_select_percentile_range(const uint16_t *frame_temp14,
                                                       uint16_t frame_temp14_min,
                                                       uint16_t frame_temp14_max,
                                                       uint16_t *disp_temp14_min,
                                                       uint16_t *disp_temp14_max)
{
    if ((frame_temp14 == NULL) || (disp_temp14_min == NULL) || (disp_temp14_max == NULL))
    {
        return;
    }
    *disp_temp14_min = frame_temp14_min;
    *disp_temp14_max = frame_temp14_max;
}

/**
 * @brief 閫夋嫨褰撳墠鏄剧ず浣跨敤鐨勬俯搴﹁寖鍥淬€? * @param frame_temp14_min 褰撳墠甯ф帹鑽愭樉绀轰笅闄愩€? * @param frame_temp14_max 褰撳墠甯ф帹鑽愭樉绀轰笂闄愩€? * @param disp_temp14_min 鏄剧ず鑼冨洿鏈€灏忓€艰緭鍑烘寚閽堛€? * @param disp_temp14_max 鏄剧ず鑼冨洿鏈€澶у€艰緭鍑烘寚閽堛€? * @return 鏃犮€? */
static void __attribute__((unused)) tiny1c_thermal_app_select_display_range(uint16_t frame_temp14_min,
                                                    uint16_t frame_temp14_max,
                                                    uint16_t *disp_temp14_min,
                                                    uint16_t *disp_temp14_max)
{
    if ((disp_temp14_min == NULL) || (disp_temp14_max == NULL))
    {
        return;
    }

    *disp_temp14_min = frame_temp14_min;
    *disp_temp14_max = frame_temp14_max;
}

/**
 * @brief 灏嗛噰闆嗗埌鐨?Y16 娓╁害甯цВ鐮佷负 Y14锛屽苟杩涜 2 鍊嶆斁澶у悗鏄剧ず鍒?LCD銆? * @param 鏃犮€? * @return 鏃犮€? */
static void tiny1c_thermal_app_render_temperature_frame_2x(void)
{
    uint16_t center_temp14;
    const uint8_t *temp_source_frame;

    SCB_InvalidateDCache_by_Addr((uint32_t *)g_tiny1c_raw_frame, (int32_t)CFG_TINY1C_FRAME_BYTES);

#if (CFG_TINY1C_OUTPUT_IMAGE_AND_TEMP_ENABLE == 1U)
    temp_source_frame = &g_tiny1c_raw_frame[CFG_TINY1C_IMAGE_FRAME_WIDTH * CFG_TINY1C_IMAGE_FRAME_HEIGHT * CFG_TINY1C_BYTES_PER_PIXEL];
    tiny1c_thermal_app_unpack_temp14_frame(temp_source_frame, g_tiny1c_temp14_frame);
    center_temp14 = g_tiny1c_temp14_frame[(CFG_TINY1C_FRAME_HEIGHT / 2U) * CFG_TINY1C_FRAME_WIDTH +
                                            (CFG_TINY1C_FRAME_WIDTH / 2U)];
    tiny1c_thermal_app_render_yuv422_image_frame_2x(g_tiny1c_raw_frame);
#else
    temp_source_frame = g_tiny1c_raw_frame;
    tiny1c_thermal_app_unpack_temp14_frame(temp_source_frame, g_tiny1c_temp14_frame);
    center_temp14 = g_tiny1c_temp14_frame[(CFG_TINY1C_FRAME_HEIGHT / 2U) * CFG_TINY1C_FRAME_WIDTH +
                                            (CFG_TINY1C_FRAME_WIDTH / 2U)];
#endif

#if (CFG_TINY1C_GUI_MODE_ENABLE == 0U)
    tiny1c_thermal_app_draw_center_temperature_overlay(center_temp14);
#endif
    (void)center_temp14;
}

/**
 * @brief 鍚姩 Tiny1-C 娓╁害娴佸簲鐢ㄣ€? * @param 鏃犮€? * @return ir_error_t锛歚IR_SUCCESS` 琛ㄧず鎴愬姛銆? */
ir_error_t tiny1c_thermal_app_start(void)
{
    PreviewStartParam_t preview_param = {0};
    ir_error_t rst;

    rst = tiny1c_vdcmd_init();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = tiny1c_thermal_app_config_module_defaults();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = tiny1c_vdcmd_set_pseudo_color(PREVIEW_PATH0, CFG_TINY1C_DEFAULT_PSEUDO_COLOR_MODE);
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

#if (CFG_TINY1C_GUI_MODE_ENABLE == 0U)
    rgblcd_init();
    rgblcd_clear(BLACK);
#endif
    tiny1c_thermal_app_display_region_init();

    preview_param.path = PREVIEW_PATH0;
    preview_param.source = 0U;
    preview_param.width = CFG_TINY1C_FRAME_WIDTH;
    preview_param.height = CFG_TINY1C_RAW_FRAME_HEIGHT;
    preview_param.fps = CFG_TINY1C_PREVIEW_FPS;
    preview_param.mode = CFG_TINY1C_PREVIEW_MODE_DVP;

    rst = tiny1c_vdcmd_preview_start(&preview_param);
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    HAL_Delay(20U);

#if (CFG_TINY1C_OUTPUT_IMAGE_AND_TEMP_ENABLE != 1U)
    rst = tiny1c_vdcmd_y16_temperature_start(PREVIEW_PATH0);
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    HAL_Delay(20U);
#endif

    if (HAL_DCMIPP_PIPE_Start(&hdcmipp, DCMIPP_PIPE0, (uint32_t)g_tiny1c_raw_frame, DCMIPP_MODE_CONTINUOUS) != HAL_OK)
    {
        return IR_CONTROL_TRANSFER_FAIL;
    }

    g_tiny1c_frame_ready = 0U;
    return IR_SUCCESS;
}

/**
 * @brief 娓╁害娴佸簲鐢ㄨ疆璇㈠鐞嗗嚱鏁般€? * @param 鏃犮€? * @return 鏃犮€? */
void tiny1c_thermal_app_process(void)
{
    if (g_tiny1c_frame_ready != 0U)
    {
        g_tiny1c_frame_ready = 0U;
        tiny1c_thermal_app_render_temperature_frame_2x();
        g_tiny1c_frame_counter++;
    }
}

/**
 * @brief DCMIPP 甯у畬鎴愪簨浠跺洖璋冨叆鍙ｃ€? * @param dcmipp_handle DCMIPP 鍙ユ焺鎸囬拡銆? * @param pipe 绠￠亾缂栧彿銆? * @return 鏃犮€? */
void tiny1c_thermal_app_on_frame_event(DCMIPP_HandleTypeDef *dcmipp_handle, uint32_t pipe)
{
    if ((dcmipp_handle != NULL) && (dcmipp_handle->Instance == DCMIPP) && (pipe == DCMIPP_PIPE0))
    {
        g_tiny1c_frame_ready = 1U;
    }
}

/**
 * @brief Get the RGB565 preview buffer rendered from the latest thermal frame.
 * @param None
 * @return const uint16_t* Pointer to the RGB565 preview buffer.
 */
const uint16_t *tiny1c_thermal_app_get_rgb565_frame(void)
{
    return g_tiny1c_rgb565_frame;
}

/**
 * @brief Get the preview buffer width in pixels.
 * @param None
 * @return uint16_t Preview width.
 */
uint16_t tiny1c_thermal_app_get_preview_width(void)
{
    return CFG_TINY1C_DISP_WIDTH;
}

/**
 * @brief Get the preview buffer height in pixels.
 * @param None
 * @return uint16_t Preview height.
 */
uint16_t tiny1c_thermal_app_get_preview_height(void)
{
    return CFG_TINY1C_DISP_HEIGHT;
}

/**
 * @brief Get the current center temperature overlay value in centi-degrees Celsius.
 * @param None
 * @return int32_t Center temperature in centi-degrees Celsius.
 */
int32_t tiny1c_thermal_app_get_center_temp_centi_c(void)
{
    return g_tiny1c_center_temp_centi_c;
}

/**
 * @brief 鑾峰彇褰撳墠娓╁害甯х紦鍐插尯銆? * @param 鏃犮€? * @return const uint16_t*锛氭俯搴﹀抚棣栧湴鍧€銆? */
const uint16_t *tiny1c_thermal_app_get_temp14_frame(void)
{
    return g_tiny1c_temp14_frame;
}

/**
 * @brief 鑾峰彇娓╁害甯у搴︺€? * @param 鏃犮€? * @return uint16_t锛氬抚瀹姐€? */
uint16_t tiny1c_thermal_app_get_frame_width(void)
{
    return CFG_TINY1C_FRAME_WIDTH;
}

/**
 * @brief 鑾峰彇娓╁害甯ч珮搴︺€? * @param 鏃犮€? * @return uint16_t锛氬抚楂樸€? */
uint16_t tiny1c_thermal_app_get_frame_height(void)
{
    return CFG_TINY1C_FRAME_HEIGHT;
}

uint32_t tiny1c_thermal_app_get_frame_counter(void)
{
    return g_tiny1c_frame_counter;
}

/**
 * @brief 璁剧疆搴旂敤灞備吉褰╅鏍笺€? * @param style 浼僵椋庢牸銆? * @return 鏃犮€? */
/**
 * @brief Trigger one manual Tiny1-C FFC/B-update cycle.
 * @param None
 * @return ir_error_t `IR_SUCCESS` indicates the request was accepted.
 */
ir_error_t tiny1c_thermal_app_force_ffc(void)
{
    return tiny1c_vdcmd_trigger_ffc();
}

void tiny1c_thermal_app_set_preview_transform(uint8_t mirror_enable, uint8_t flip_enable)
{
    g_tiny1c_preview_mirror_enable = (mirror_enable != 0U) ? 1U : 0U;
    g_tiny1c_preview_flip_enable = (flip_enable != 0U) ? 1U : 0U;
}

void tiny1c_thermal_app_set_preview_contrast(uint8_t contrast_slider_value)
{
    if (contrast_slider_value > 100U)
    {
        contrast_slider_value = 100U;
    }

    g_tiny1c_preview_contrast_slider = contrast_slider_value;
}

uint8_t tiny1c_thermal_app_get_preview_mirror_enabled(void)
{
    return g_tiny1c_preview_mirror_enable;
}

uint8_t tiny1c_thermal_app_get_preview_flip_enabled(void)
{
    return g_tiny1c_preview_flip_enable;
}

/**
 * @brief Map one raw temp14 frame coordinate into the current preview-oriented frame coordinate.
 * @param source_x Source-frame X coordinate in range 0..(CFG_TINY1C_FRAME_WIDTH - 1).
 * @param source_y Source-frame Y coordinate in range 0..(CFG_TINY1C_FRAME_HEIGHT - 1).
 * @param dest_x Pointer to the transformed X coordinate, may be NULL.
 * @param dest_y Pointer to the transformed Y coordinate, may be NULL.
 * @return None
 */
void tiny1c_thermal_app_transform_frame_point(uint16_t source_x,
                                              uint16_t source_y,
                                              uint16_t *dest_x,
                                              uint16_t *dest_y)
{
    uint16_t transformed_x = source_x;
    uint16_t transformed_y = source_y;

    if (source_x >= CFG_TINY1C_FRAME_WIDTH)
    {
        transformed_x = (uint16_t)(CFG_TINY1C_FRAME_WIDTH - 1U);
    }

    if (source_y >= CFG_TINY1C_FRAME_HEIGHT)
    {
        transformed_y = (uint16_t)(CFG_TINY1C_FRAME_HEIGHT - 1U);
    }

    if (g_tiny1c_preview_mirror_enable != 0U)
    {
        transformed_x = (uint16_t)((CFG_TINY1C_FRAME_WIDTH - 1U) - transformed_x);
    }

    if (g_tiny1c_preview_flip_enable != 0U)
    {
        transformed_y = (uint16_t)((CFG_TINY1C_FRAME_HEIGHT - 1U) - transformed_y);
    }

    if (dest_x != NULL)
    {
        *dest_x = transformed_x;
    }

    if (dest_y != NULL)
    {
        *dest_y = transformed_y;
    }
}
