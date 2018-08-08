#ifndef ANDROID_HW_TYPES_H
#define ANDROID_HW_TYPES_H
#include <stdint.h>

#define BASE_OFFSET 8*1024
#define DEFAULT_BRIGHTNESS  50
#define DEFAULT_CONTRAST  50
#define DEFAULT_SATURATION  50
#define DEFAULT_HUE  50
#define DEFAULT_OVERSCAN_VALUE 100

#define OVERSCAN_LEFT 0
#define OVERSCAN_TOP 1
#define OVERSCAN_RIGHT 2
#define OVERSCAN_BOTTOM 3

struct lut_data{
    uint16_t size;
    uint16_t lred[1024];
    uint16_t lgreen[1024];
    uint16_t lblue[1024];
};

struct lut_info{
    struct lut_data main;
    struct lut_data aux;
};

#define BUFFER_LENGTH    256
#define AUTO_BIT_RESET 0x00
#define RESOLUTION_AUTO 1<<0
#define COLOR_AUTO (1<<1)
#define HDCP1X_EN (1<<2)
#define RESOLUTION_WHITE_EN (1<<3)

struct drm_display_mode {
    /* Proposed mode values */
    int clock;      /* in kHz */
    int hdisplay;
    int hsync_start;
    int hsync_end;
    int htotal;
    int vdisplay;
    int vsync_start;
    int vsync_end;
    int vtotal;
    int vrefresh;
    int vscan;
    unsigned int flags;
    int picture_aspect_ratio;
};

enum output_format {
    output_rgb=0,
    output_ycbcr444=1,
    output_ycbcr422=2,
    output_ycbcr420=3,
    output_ycbcr_high_subsampling=4,  // (YCbCr444 > YCbCr422 > YCbCr420 > RGB)
    output_ycbcr_low_subsampling=5  , // (RGB > YCbCr420 > YCbCr422 > YCbCr444)
    invalid_output=6,
};

enum  output_depth{
    Automatic=0,
    depth_24bit=8,
    depth_30bit=10,
};

struct overscan {
    unsigned int maxvalue;
    unsigned short leftscale;
    unsigned short rightscale;
    unsigned short topscale;
    unsigned short bottomscale;
};

struct hwc_inital_info{
    char device[128];
    unsigned int framebuffer_width;
    unsigned int framebuffer_height;
    float fps;
};

struct bcsh_info {
    unsigned short brightness;
    unsigned short contrast;
    unsigned short saturation;
    unsigned short hue;
};

struct screen_info {
    int type;
    struct drm_display_mode resolution;// 52 bytes
    enum output_format  format; // 4 bytes
    enum output_depth depthc; // 4 bytes
    unsigned int feature;//4 //4 bytes
};

struct disp_info {
    struct screen_info screen_list[5];
    struct overscan scan;//12 bytes
    struct hwc_inital_info hwc_info; //140 bytes
    struct bcsh_info bcsh;
    unsigned int reserve[128];
    struct lut_data mlutdata;/*6k+4*/
};

struct file_base_paramer
{
    struct disp_info main;
    struct disp_info aux;
};
#endif
