/*
 * Copyright 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "hw_output"

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>

#include <cutils/native_handle.h>
#include <log/log.h>

#include <hardware/hw_output.h>
#include <stdio.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cutils/properties.h>
#include <drm_fourcc.h>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <inttypes.h>

#include "rkdisplay/drmresources.h"
#include "rkdisplay/drmmode.h"
#include "rkdisplay/drmconnector.h"

using namespace android;
/*****************************************************************************/
typedef struct hw_output_private {
    hw_output_device_t device;

    // Callback related data
    void* callback_data;
    DrmResources *drm_;
    DrmConnector* primary;
    DrmConnector* extend;
    struct lut_info* mlut;
} hw_output_private_t;

static int hw_output_device_open(const struct hw_module_t* module,
        const char* name, struct hw_device_t** device);

static struct hw_module_methods_t hw_output_module_methods = {
    .open = hw_output_device_open
};

hw_output_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .version_major = 1,
        .version_minor = 0,
        .id = HW_OUTPUT_HARDWARE_MODULE_ID,
        .name = "Sample hw output module",
        .author = "The Android Open Source Project",
        .methods = &hw_output_module_methods,
        .dso = NULL,
        .reserved = {0},
    }
};

static char const *const device_template[] =
{
    "/dev/block/platform/1021c000.dwmmc/by-name/baseparameter",
    "/dev/block/platform/30020000.dwmmc/by-name/baseparameter",
    "/dev/block/platform/fe330000.sdhci/by-name/baseparameter",
    "/dev/block/platform/ff520000.dwmmc/by-name/baseparameter",
    "/dev/block/platform/ff0f0000.dwmmc/by-name/baseparameter",
    "/dev/block/rknand_baseparameter",
    NULL
};

const char* GetBaseparameterFile(void)
{
    int i = 0;

    while (device_template[i]) {
        if (!access(device_template[i], R_OK | W_OK))
            return device_template[i];
        ALOGD("temp[%d]=%s access=%d(%s)", i,device_template[i], errno, strerror(errno));
        i++;
    }
    return NULL;
}

static bool builtInHdmi(int type){
    return type == DRM_MODE_CONNECTOR_HDMIA || type == DRM_MODE_CONNECTOR_HDMIB;
}
static void freeLutInfo(struct lut_info* mlut){
    if (mlut) {
        free(mlut);
        mlut=NULL;
    }
}

static void checkBcshInfo(uint32_t* mBcsh)
{
    if (mBcsh[0] < 0)
        mBcsh[0] = 0;
    else if (mBcsh[0] > 100)
        mBcsh[0] = 100;

    if (mBcsh[1] < 0)
        mBcsh[1] = 0;
    else if (mBcsh[1] > 100)
        mBcsh[1] = 100;

    if (mBcsh[2] < 0)
        mBcsh[2] = 0;
    else if (mBcsh[2] > 100)
        mBcsh[2] = 100;

    if (mBcsh[3] < 0)
        mBcsh[3] = 0;
    else if (mBcsh[3] > 100)
        mBcsh[3] = 100;
}

static bool getBaseParameterInfo(struct file_base_paramer* base_paramer)
{
    int file;
    const char *baseparameterfile = GetBaseparameterFile();
    if (baseparameterfile) {
        file = open(baseparameterfile, O_RDWR);
        if (file > 0) {
            unsigned int length = lseek(file, 0L, SEEK_END);

            lseek(file, 0L, SEEK_SET);
            ALOGD("getBaseParameterInfo size=%d", (int)sizeof(*base_paramer));
            if (length >  sizeof(*base_paramer)) {
                read(file, (void*)&(base_paramer->main), sizeof(base_paramer->main));
                lseek(file, BASE_OFFSET, SEEK_SET);
                read(file, (void*)&(base_paramer->aux), sizeof(base_paramer->aux));
                return true;
            }
        }
    }
    return false;
}

static int findSuitableInfoSlot(struct disp_info* info, int type)
{
    int found=0;
    for (int i=0;i<5;i++) {
        if (info->screen_list[i].type !=0 && info->screen_list[i].type == type) {
            found = i;
            break;
        } else if (info->screen_list[i].type !=0 && found == false){
            found++;
        }
    }
    if (found == -1) {
        found = 0;
        ALOGD("noting saved, used the first slot");
    }
    ALOGD("findSuitableInfoSlot: %d type=%d", found, type);
    return found;
}

static bool getResolutionInfo(hw_output_private_t *priv, int dpy, char* resolution)
{
    drmModePropertyBlobPtr blob;
    drmModeObjectPropertiesPtr props;
    DrmConnector* mCurConnector = NULL;
    DrmCrtc *crtc = NULL;
    struct drm_mode_modeinfo *drm_mode;
    struct file_base_paramer base_paramer;
    int value;
    bool found = false;
    int slot = 0;

    if (dpy == HWC_DISPLAY_PRIMARY) {
        mCurConnector = priv->primary;
    } else if(dpy == HWC_DISPLAY_EXTERNAL) {
        mCurConnector = priv->extend;
    }

    if (getBaseParameterInfo(&base_paramer)) {
        if (dpy == HWC_DISPLAY_PRIMARY) {
            slot = findSuitableInfoSlot(&base_paramer.main, mCurConnector->get_type());
            if (!base_paramer.main.screen_list[slot].resolution.hdisplay ||
                    !base_paramer.main.screen_list[slot].resolution.clock ||
                    !base_paramer.main.screen_list[slot].resolution.vdisplay) {
                sprintf(resolution, "%s", "Auto");
                return resolution;
            }
        } else if (dpy == HWC_DISPLAY_EXTERNAL) {
            slot = findSuitableInfoSlot(&base_paramer.aux, mCurConnector->get_type());
            if (!base_paramer.aux.screen_list[slot].resolution.hdisplay ||
                    !base_paramer.aux.screen_list[slot].resolution.clock ||
                    !base_paramer.aux.screen_list[slot].resolution.vdisplay) {
                sprintf(resolution, "%s", "Auto");
                return resolution;
            }
        }
    }

    if (mCurConnector != NULL) {
        crtc = priv->drm_->GetCrtcFromConnector(mCurConnector);
        if (crtc == NULL) {
            return false;
        }
        props = drmModeObjectGetProperties(priv->drm_->fd(), crtc->id(), DRM_MODE_OBJECT_CRTC);
        for (int i = 0; !found && (size_t)i < props->count_props; ++i) {
            drmModePropertyPtr p = drmModeGetProperty(priv->drm_->fd(), props->props[i]);
            if (!strcmp(p->name, "MODE_ID")) {
                found = true;
                if (!drm_property_type_is(p, DRM_MODE_PROP_BLOB)) {
                    ALOGE("%s:line=%d,is not blob",__FUNCTION__,__LINE__);
                    drmModeFreeProperty(p);
                    drmModeFreeObjectProperties(props);
                    return false;
                }
                if (!p->count_blobs)
                    value = props->prop_values[i];
                else
                    value = p->blob_ids[0];
                blob = drmModeGetPropertyBlob(priv->drm_->fd(), value);
                if (!blob) {
                    ALOGE("%s:line=%d, blob is null",__FUNCTION__,__LINE__);
                    drmModeFreeProperty(p);
                    drmModeFreeObjectProperties(props);
                    return false;
                }

                float vfresh;
                drm_mode = (struct drm_mode_modeinfo *)blob->data;
                if (drm_mode->flags & DRM_MODE_FLAG_INTERLACE)
                    vfresh = drm_mode->clock *2/ (float)(drm_mode->vtotal * drm_mode->htotal) * 1000.0f;
                else
                    vfresh = drm_mode->clock / (float)(drm_mode->vtotal * drm_mode->htotal) * 1000.0f;
                ALOGD("nativeGetCurMode: crtc_id=%d clock=%d w=%d %d %d %d %d %d flag=0x%x vfresh %.2f drm.vrefresh=%.2f", 
                        crtc->id(), drm_mode->clock, drm_mode->hdisplay, drm_mode->hsync_start,
                        drm_mode->hsync_end, drm_mode->vdisplay, drm_mode->vsync_start, drm_mode->vsync_end, drm_mode->flags,
                        vfresh, (float)drm_mode->vrefresh);
                sprintf(resolution, "%dx%d@%.2f-%d-%d-%d-%d-%d-%d-%x", drm_mode->hdisplay, drm_mode->vdisplay, vfresh,
                        drm_mode->hsync_start, drm_mode->hsync_end, drm_mode->htotal,
                        drm_mode->vsync_start, drm_mode->vsync_end, drm_mode->vtotal,
                        drm_mode->flags);
                drmModeFreePropertyBlob(blob);
            }
            drmModeFreeProperty(p);
        }
        drmModeFreeObjectProperties(props);
    } else {
        return false;
    }

    return true;
}

static void updateConnectors(hw_output_private_t *priv){
    if (priv->drm_->connectors().size() == 2) {
        bool foundHdmi=false;
        int cnt=0,crtcId1=0,crtcId2=0;
        for (auto &conn : priv->drm_->connectors()) {
            if (cnt == 0 && priv->drm_->GetCrtcFromConnector(conn.get())) {
                ALOGD("encoderId1: %d", conn->encoder()->id());
                crtcId1 = priv->drm_->GetCrtcFromConnector(conn.get())->id();
            } else if (priv->drm_->GetCrtcFromConnector(conn.get())){
                ALOGD("encoderId2: %d", conn->encoder()->id());
                crtcId2 = priv->drm_->GetCrtcFromConnector(conn.get())->id();
            }

            if (builtInHdmi(conn->get_type()))
                foundHdmi=true;
            cnt++;
        }
        ALOGD("crtc: %d %d foundHdmi %d 2222", crtcId1, crtcId2, foundHdmi);
        char property[PROPERTY_VALUE_MAX];
        property_get("vendor.hwc.device.primary", property, "null");
        if (crtcId1 == crtcId2 && foundHdmi && strstr(property, "HDMI-A") == NULL) {
            for (auto &conn : priv->drm_->connectors()) {
                if (builtInHdmi(conn->get_type()) && conn->state() == DRM_MODE_CONNECTED) {
                    priv->extend = conn.get();
                    conn->set_display(1);
                } else if(!builtInHdmi(conn->get_type()) && conn->state() == DRM_MODE_CONNECTED) {
                    priv->primary = conn.get();
                    conn->set_display(0);
                }
            }
        } else {
            priv->primary = priv->drm_->GetConnectorFromType(HWC_DISPLAY_PRIMARY);
            priv->extend = priv->drm_->GetConnectorFromType(HWC_DISPLAY_EXTERNAL);
        }

    } else {
        priv->primary = priv->drm_->GetConnectorFromType(HWC_DISPLAY_PRIMARY);
        priv->extend = priv->drm_->GetConnectorFromType(HWC_DISPLAY_EXTERNAL);
    }
}

static void saveBcshConfig(struct file_base_paramer *base_paramer, int dpy){
    if (dpy == HWC_DISPLAY_PRIMARY_BIT){
        char property[PROPERTY_VALUE_MAX];

        memset(property,0,sizeof(property));
        property_get("persist.vendor.brightness.main", property, "0");
        if (atoi(property) > 0)
            base_paramer->main.bcsh.brightness = atoi(property);
        else
            base_paramer->main.bcsh.brightness = DEFAULT_BRIGHTNESS;

        memset(property,0,sizeof(property));
        property_get("persist.vendor.contrast.main", property, "0");
        if (atoi(property) > 0)
            base_paramer->main.bcsh.contrast = atoi(property);
        else
            base_paramer->main.bcsh.contrast = DEFAULT_CONTRAST;

        memset(property,0,sizeof(property));
        property_get("persist.vendor.saturation.main", property, "0");
        if (atoi(property) > 0)
            base_paramer->main.bcsh.saturation = atoi(property);
        else
            base_paramer->main.bcsh.saturation = DEFAULT_SATURATION;

        memset(property,0,sizeof(property));
        property_get("persist.vendor.hue.main", property, "0");
        if (atoi(property) > 0)
            base_paramer->main.bcsh.hue = atoi(property);
        else
            base_paramer->main.bcsh.hue = DEFAULT_HUE;
    } else {
        char property[PROPERTY_VALUE_MAX];

        memset(property,0,sizeof(property));
        property_get("persist.vendor.brightness.aux", property, "0");
        if (atoi(property) > 0)
            base_paramer->aux.bcsh.brightness = atoi(property);
        else
            base_paramer->aux.bcsh.brightness = DEFAULT_BRIGHTNESS;

        memset(property,0,sizeof(property));
        property_get("persist.vendor.contrast.aux", property, "0");
        if (atoi(property) > 0)
            base_paramer->aux.bcsh.contrast = atoi(property);
        else
            base_paramer->aux.bcsh.contrast = DEFAULT_CONTRAST;

        memset(property,0,sizeof(property));
        property_get("persist.vendor.saturation.aux", property, "0");
        if (atoi(property) > 0)
            base_paramer->aux.bcsh.saturation = atoi(property);
        else
            base_paramer->aux.bcsh.saturation = DEFAULT_SATURATION;

        memset(property,0,sizeof(property));
        property_get("persist.vendor.hue.aux", property, "0");
        if (atoi(property) > 0)
            base_paramer->aux.bcsh.hue = atoi(property);
        else
            base_paramer->aux.bcsh.hue = DEFAULT_HUE;
    }
}

/*****************************************************************************/
static void hw_output_save_config(struct hw_output_device* dev){
    hw_output_private_t* priv = (hw_output_private_t*)dev;
    char buf[BUFFER_LENGTH];
    bool isMainHdmiConnected=false;
    bool isAuxHdmiConnected = false;
    int foundMainIdx=-1,foundAuxIdx=-1;
    struct file_base_paramer base_paramer;
    DrmResources *drm_ = priv->drm_;
    DrmConnector* primary = priv->primary;
    DrmConnector* extend = priv->extend;

    if (primary != NULL) {
        std::vector<DrmMode> mModes = primary->modes();
        char resolution[PROPERTY_VALUE_MAX];
        unsigned int w=0,h=0,hsync_start=0,hsync_end=0,htotal=0;
        unsigned int vsync_start=0,vsync_end=0,vtotal=0,flags=0;
        float vfresh=0.0000;

        property_get("persist.vendor.resolution.main", resolution, "0x0@0.00-0-0-0-0-0-0-0");
        if (strncmp(resolution, "Auto", 4) != 0 && strncmp(resolution, "0x0p0-0", 7) !=0)
            sscanf(resolution,"%dx%d@%f-%d-%d-%d-%d-%d-%d-%x", &w, &h, &vfresh, &hsync_start,&hsync_end,
                    &htotal,&vsync_start,&vsync_end, &vtotal, &flags);
        for (size_t c = 0; c < mModes.size(); ++c){
            const DrmMode& info = mModes[c];
            char curDrmModeRefresh[16];
            char curRefresh[16];
            float mModeRefresh;
            if (info.flags() & DRM_MODE_FLAG_INTERLACE)
                mModeRefresh = info.clock()*2 / (float)(info.v_total()* info.h_total()) * 1000.0f;
            else
                mModeRefresh = info.clock()/ (float)(info.v_total()* info.h_total()) * 1000.0f;
            sprintf(curDrmModeRefresh, "%.2f", mModeRefresh);
            sprintf(curRefresh, "%.2f", vfresh);
            if (info.h_display() == w &&
                    info.v_display() == h &&
                    info.h_sync_start() == hsync_start &&
                    info.h_sync_end() == hsync_end &&
                    info.h_total() == htotal &&
                    info.v_sync_start() == vsync_start &&
                    info.v_sync_end() == vsync_end &&
                    info.v_total()==vtotal &&
                    atof(curDrmModeRefresh)==atof(curRefresh)) {
                ALOGD("***********************found main idx %d ****************", (int)c);
                foundMainIdx = c;
                sprintf(buf, "display=%d,iface=%d,enable=%d,mode=%s\n",
                        primary->display(), primary->get_type(), primary->state(), resolution);
                break;
            }
        }
    }

    if (extend != NULL) {
        std::vector<DrmMode> mModes = extend->modes();
        char resolution[PROPERTY_VALUE_MAX];
        unsigned int w=0,h=0,hsync_start=0,hsync_end=0,htotal=0;
        unsigned int vsync_start=0,vsync_end=0,vtotal=0,flags;
        float vfresh=0;

        property_get("persist.vendor.resolution.aux", resolution, "0x0@0.00-0-0-0-0-0-0-0");
        if (strncmp(resolution, "Auto", 4) != 0 && strncmp(resolution, "0x0p0-0", 7) !=0)
            sscanf(resolution,"%dx%d@%f-%d-%d-%d-%d-%d-%d-%x", &w, &h, &vfresh, &hsync_start,&hsync_end,&htotal,&vsync_start,&vsync_end,
                    &vtotal, &flags);
        for (size_t c = 0; c < mModes.size(); ++c){
            const DrmMode& info = mModes[c];
            char curDrmModeRefresh[16];
            char curRefresh[16];
            float mModeRefresh;
            if (info.flags() & DRM_MODE_FLAG_INTERLACE)
                mModeRefresh = info.clock()*2 / (float)(info.v_total()* info.h_total()) * 1000.0f;
            else
                mModeRefresh = info.clock()/ (float)(info.v_total()* info.h_total()) * 1000.0f;
            sprintf(curDrmModeRefresh, "%.2f", mModeRefresh);
            sprintf(curRefresh, "%.2f", vfresh);
            if (info.h_display() == w &&
                    info.v_display() == h &&
                    info.h_sync_start() == hsync_start &&
                    info.h_sync_end() == hsync_end &&
                    info.h_total() == htotal &&
                    info.v_sync_start() == vsync_start &&
                    info.v_sync_end() == vsync_end &&
                    info.v_total()==vtotal &&
                    atof(curDrmModeRefresh)==atoi(curRefresh)) {
                ALOGD("***********************found aux idx %d ****************", (int)c);
                foundAuxIdx = c;
                break;
            }
        }
    }

    int file;
    const char *baseparameterfile = GetBaseparameterFile();
    if (!baseparameterfile) {
        sync();
        return;
    }
    file = open(baseparameterfile, O_RDWR);
    if (file < 0) {
        ALOGW("base paramter file can not be opened");
        sync();
        return;
    }
    // caculate file's size and read it
    unsigned int length = lseek(file, 0L, SEEK_END);
    lseek(file, 0L, SEEK_SET);
    if(length < sizeof(base_paramer)) {
        ALOGE("BASEPARAME data's length is error\n");
        sync();
        close(file);
        return;
    }

    read(file, (void*)&(base_paramer.main), sizeof(base_paramer.main));
    lseek(file, BASE_OFFSET, SEEK_SET);
    read(file, (void*)&(base_paramer.aux), sizeof(base_paramer.aux));

    for (auto &conn : drm_->connectors()) {
        if (conn->state() == DRM_MODE_CONNECTED
            && (conn->get_type() == DRM_MODE_CONNECTOR_HDMIA)
            && (conn->possible_displays() & HWC_DISPLAY_PRIMARY_BIT))
            isMainHdmiConnected = true;
        else if(conn->state() == DRM_MODE_CONNECTED
                && (conn->get_type() == DRM_MODE_CONNECTOR_HDMIA)
                && (conn->possible_displays() & HWC_DISPLAY_EXTERNAL_BIT))
            isAuxHdmiConnected = true;
    }
    ALOGD("nativeSaveConfig: size=%d isMainHdmiConnected=%d", (int)sizeof(base_paramer.main), isMainHdmiConnected);
    for (auto &conn : drm_->connectors()) {
        if (conn->state() == DRM_MODE_CONNECTED
            && (conn->possible_displays() & HWC_DISPLAY_PRIMARY_BIT)) {
            char property[PROPERTY_VALUE_MAX];
            int w=0,h=0,hsync_start=0,hsync_end=0,htotal=0;
            int vsync_start=0,vsync_end=0,vtotal=0,flags=0;
            int left=0,top=0,right=0,bottom=0;
            float vfresh=0;
            int slot = findSuitableInfoSlot(&base_paramer.main, conn->get_type());
            if (isMainHdmiConnected && conn->get_type() == DRM_MODE_CONNECTOR_TV)
                continue;

            base_paramer.main.screen_list[slot].type = conn->get_type();
            base_paramer.main.screen_list[slot].feature &= AUTO_BIT_RESET;
            property_get("persist.vendor.resolution.main", property, "0x0@0.00-0-0-0-0-0-0-0");
            if (strncmp(property, "Auto", 4) != 0 && strncmp(property, "0x0p0-0", 7) !=0) {
                ALOGD("saveConfig resolution = %s", property);
                std::vector<DrmMode> mModes = primary->modes();
                sscanf(property,"%dx%d@%f-%d-%d-%d-%d-%d-%d-%x", &w, &h, &vfresh, &hsync_start,&hsync_end,&htotal,&vsync_start,&vsync_end,
                        &vtotal, &flags);

                ALOGD("last base_paramer.main.resolution.hdisplay = %d,  vdisplay=%d(%s@%f)",
                        base_paramer.main.screen_list[slot].resolution.hdisplay,
                        base_paramer.main.screen_list[slot].resolution.vdisplay,
                        base_paramer.main.hwc_info.device,	base_paramer.main.hwc_info.fps);
                base_paramer.main.screen_list[slot].resolution.hdisplay = w;
                base_paramer.main.screen_list[slot].resolution.vdisplay = h;
                base_paramer.main.screen_list[slot].resolution.hsync_start = hsync_start;
                base_paramer.main.screen_list[slot].resolution.hsync_end = hsync_end;
                if (foundMainIdx != -1)
                    base_paramer.main.screen_list[slot].resolution.clock = mModes[foundMainIdx].clock();
                else if (flags & DRM_MODE_FLAG_INTERLACE)
                    base_paramer.main.screen_list[slot].resolution.clock = (htotal*vtotal*vfresh/2)/1000.0f;
                else
                    base_paramer.main.screen_list[slot].resolution.clock = (htotal*vtotal*vfresh)/1000.0f;
                base_paramer.main.screen_list[slot].resolution.htotal = htotal;
                base_paramer.main.screen_list[slot].resolution.vsync_start = vsync_start;
                base_paramer.main.screen_list[slot].resolution.vsync_end = vsync_end;
                base_paramer.main.screen_list[slot].resolution.vtotal = vtotal;
                base_paramer.main.screen_list[slot].resolution.flags = flags;
                ALOGD("saveBaseParameter foundMainIdx=%d clock=%d", foundMainIdx, base_paramer.main.screen_list[slot].resolution.clock);
            } else {
                base_paramer.main.screen_list[slot].feature|= RESOLUTION_AUTO;
                memset(&base_paramer.main.screen_list[slot].resolution, 0, sizeof(base_paramer.main.screen_list[slot].resolution));
            }

            memset(property,0,sizeof(property));
            property_get("persist.vendor.overscan.main", property, "overscan 100,100,100,100");
            sscanf(property, "overscan %d,%d,%d,%d",
                    &left,
                    &top,
                    &right,
                    &bottom);
            base_paramer.main.scan.leftscale = (unsigned short)left;
            base_paramer.main.scan.topscale = (unsigned short)top;
            base_paramer.main.scan.rightscale = (unsigned short)right;
            base_paramer.main.scan.bottomscale = (unsigned short)bottom;

            memset(property,0,sizeof(property));
            property_get("persist.vendor.color.main", property, "Auto");
            if (strncmp(property, "Auto", 4) != 0){
                if (strstr(property, "RGB") != 0)
                    base_paramer.main.screen_list[slot].format = output_rgb;
                else if (strstr(property, "YCBCR444") != 0)
                    base_paramer.main.screen_list[slot].format = output_ycbcr444;
                else if (strstr(property, "YCBCR422") != 0)
                    base_paramer.main.screen_list[slot].format = output_ycbcr422;
                else if (strstr(property, "YCBCR420") != 0)
                    base_paramer.main.screen_list[slot].format = output_ycbcr420;
                else {
                    base_paramer.main.screen_list[slot].feature |= COLOR_AUTO;
                    base_paramer.main.screen_list[slot].format = output_ycbcr_high_subsampling;
                }

                if (strstr(property, "8bit") != NULL)
                    base_paramer.main.screen_list[slot].depthc = depth_24bit;
                else if (strstr(property, "10bit") != NULL)
                    base_paramer.main.screen_list[slot].depthc = depth_30bit;
                else
                    base_paramer.main.screen_list[slot].depthc = Automatic;
                ALOGD("saveConfig: color=%d-%d", base_paramer.main.screen_list[slot].format, base_paramer.main.screen_list[slot].depthc);
            } else {
                base_paramer.main.screen_list[slot].depthc = Automatic;
                base_paramer.main.screen_list[slot].format = output_ycbcr_high_subsampling;
                base_paramer.main.screen_list[slot].feature |= COLOR_AUTO;
            }

            memset(property,0,sizeof(property));
            property_get("persist.vendor.hdcp1x.main", property, "0");
            if (atoi(property) > 0)
                base_paramer.main.screen_list[slot].feature |= HDCP1X_EN;

            memset(property,0,sizeof(property));
            property_get("persist.vendor.resolution_white.main", property, "0");
            if (atoi(property) > 0)
                base_paramer.main.screen_list[slot].feature |= RESOLUTION_WHITE_EN;
            saveBcshConfig(&base_paramer, HWC_DISPLAY_PRIMARY_BIT);
#ifdef TEST_BASE_PARMARTER
            /*save aux fb & device*/
            saveHwcInitalInfo(&base_paramer, HWC_DISPLAY_PRIMARY_BIT);
#endif
        } else if(conn->state() == DRM_MODE_CONNECTED
                && (conn->possible_displays() & HWC_DISPLAY_EXTERNAL_BIT)
                && (conn->encoder() != NULL)) {
            char property[PROPERTY_VALUE_MAX];
            int w=0,h=0,hsync_start=0,hsync_end=0,htotal=0;
            int vsync_start=0,vsync_end=0,vtotal=0,flags=0;
            float vfresh=0;
            int left=0,top=0,right=0,bottom=0;
            int slot = findSuitableInfoSlot(&base_paramer.aux, conn->get_type());

            if (isAuxHdmiConnected && conn->get_type() == DRM_MODE_CONNECTOR_TV)
                continue;

            base_paramer.aux.screen_list[slot].type = conn->get_type();
            base_paramer.aux.screen_list[slot].feature &= AUTO_BIT_RESET;
            property_get("persist.vendor.resolution.aux", property, "0x0p0-0");
            if (strncmp(property, "Auto", 4) != 0 && strncmp(property, "0x0p0-0", 7) !=0) {
                std::vector<DrmMode> mModes = extend->modes();
                sscanf(property,"%dx%d@%f-%d-%d-%d-%d-%d-%d-%x", &w, &h, &vfresh, &hsync_start,&hsync_end,&htotal,&vsync_start,&vsync_end,
                        &vtotal, &flags);
                base_paramer.aux.screen_list[slot].resolution.hdisplay = w;
                base_paramer.aux.screen_list[slot].resolution.vdisplay = h;
                if (foundMainIdx != -1)
                    base_paramer.aux.screen_list[slot].resolution.clock = mModes[foundMainIdx].clock();
                else if (flags & DRM_MODE_FLAG_INTERLACE)
                    base_paramer.aux.screen_list[slot].resolution.clock = (htotal*vtotal*vfresh/2) / 1000.0f;
                else
                    base_paramer.aux.screen_list[slot].resolution.clock = (htotal*vtotal*vfresh) / 1000.0f;
                base_paramer.aux.screen_list[slot].resolution.hsync_start = hsync_start;
                base_paramer.aux.screen_list[slot].resolution.hsync_end = hsync_end;
                base_paramer.aux.screen_list[slot].resolution.htotal = htotal;
                base_paramer.aux.screen_list[slot].resolution.vsync_start = vsync_start;
                base_paramer.aux.screen_list[slot].resolution.vsync_end = vsync_end;
                base_paramer.aux.screen_list[slot].resolution.vtotal = vtotal;
                base_paramer.aux.screen_list[slot].resolution.flags = flags;
            } else {
                base_paramer.aux.screen_list[slot].feature |= RESOLUTION_AUTO;
                memset(&base_paramer.aux.screen_list[slot].resolution, 0, sizeof(base_paramer.aux.screen_list[slot].resolution));
            }

            memset(property,0,sizeof(property));
            property_get("persist.vendor.overscan.aux", property, "overscan 100,100,100,100");
            sscanf(property, "overscan %d,%d,%d,%d",
                    &left,
                    &top,
                    &right,
                    &bottom);
            base_paramer.aux.scan.leftscale = (unsigned short)left;
            base_paramer.aux.scan.topscale = (unsigned short)top;
            base_paramer.aux.scan.rightscale = (unsigned short)right;
            base_paramer.aux.scan.bottomscale = (unsigned short)bottom;

            memset(property,0,sizeof(property));
            property_get("persist.vendor.color.aux", property, "Auto");
            if (strncmp(property, "Auto", 4) != 0){
                char color[16];
                char depth[16];

                sscanf(property, "%s-%s", color, depth);
                if (strncmp(color, "RGB", 3) == 0)
                    base_paramer.aux.screen_list[slot].format = output_rgb;
                else if (strncmp(color, "YCBCR444", 8) == 0)
                    base_paramer.aux.screen_list[slot].format = output_ycbcr444;
                else if (strncmp(color, "YCBCR422", 8) == 0)
                    base_paramer.aux.screen_list[slot].format = output_ycbcr422;
                else if (strncmp(color, "YCBCR420", 8) == 0)
                    base_paramer.aux.screen_list[slot].format = output_ycbcr420;
                else {
                    base_paramer.aux.screen_list[slot].feature |= COLOR_AUTO;
                    base_paramer.aux.screen_list[slot].format = output_ycbcr_high_subsampling;
                }

                if (strncmp(depth, "8bit", 4) == 0)
                    base_paramer.aux.screen_list[slot].depthc = depth_24bit;
                else if (strncmp(depth, "10bit", 5) == 0)
                    base_paramer.aux.screen_list[slot].depthc = depth_30bit;
                else
                    base_paramer.aux.screen_list[slot].depthc = Automatic;
            } else {
                base_paramer.aux.screen_list[slot].feature |= COLOR_AUTO;
                base_paramer.aux.screen_list[slot].depthc = Automatic;
                base_paramer.aux.screen_list[slot].format = output_ycbcr_high_subsampling;
            }

            memset(property,0,sizeof(property));
            property_get("persist.vendor.hdcp1x.aux", property, "0");
            if (atoi(property) > 0)
                base_paramer.aux.screen_list[slot].feature |= HDCP1X_EN;

            memset(property,0,sizeof(property));
            property_get("persist.vendor.resolution_white.aux", property, "0");
            if (atoi(property) > 0)
                base_paramer.aux.screen_list[slot].feature |= RESOLUTION_WHITE_EN;
            /*add for BCSH*/
            saveBcshConfig(&base_paramer, HWC_DISPLAY_EXTERNAL_BIT);
#ifdef TEST_BASE_PARMARTER
            /*save aux fb & device*/
            saveHwcInitalInfo(&base_paramer, HWC_DISPLAY_EXTERNAL_BIT);
#endif
        }
    }

    if (priv->mlut != NULL) {
        int mainLutSize = priv->mlut->main.size*sizeof(uint16_t);
        int auxLutSize = priv->mlut->aux.size*sizeof(uint16_t);
        if (mainLutSize) {
            base_paramer.main.mlutdata.size = priv->mlut->main.size;
            memcpy(base_paramer.main.mlutdata.lred, priv->mlut->main.lred, mainLutSize);
            memcpy(base_paramer.main.mlutdata.lgreen, priv->mlut->main.lgreen, mainLutSize);
            memcpy(base_paramer.main.mlutdata.lblue, priv->mlut->main.lblue, mainLutSize);
        }

        if (auxLutSize) {
            base_paramer.aux.mlutdata.size = priv->mlut->aux.size;
            memcpy(base_paramer.aux.mlutdata.lred, priv->mlut->aux.lred, mainLutSize);
            memcpy(base_paramer.aux.mlutdata.lgreen, priv->mlut->aux.lgreen, mainLutSize);
            memcpy(base_paramer.aux.mlutdata.lblue, priv->mlut->aux.lblue, mainLutSize);
        }
    }
    freeLutInfo(priv->mlut);
    lseek(file, 0L, SEEK_SET);
    write(file, (char*)(&base_paramer.main), sizeof(base_paramer.main));
    lseek(file, BASE_OFFSET, SEEK_SET);
    write(file, (char*)(&base_paramer.aux), sizeof(base_paramer.aux));
    close(file);
    sync();
    /*
       ALOGD("[%s] hdmi:%d,%d,%d,%d,%d,%d foundMainIdx %d\n", __FUNCTION__,
       base_paramer.main.resolution.hdisplay,
       base_paramer.main.resolution.vdisplay,
       base_paramer.main.resolution.hsync_start,
       base_paramer.main.resolution.hsync_end,
       base_paramer.main.resolution.htotal,
       base_paramer.main.resolution.flags,
       foundMainIdx);

       ALOGD("[%s] tve:%d,%d,%d,%d,%d,%d foundAuxIdx %d\n", __FUNCTION__,
       base_paramer.aux.resolution.hdisplay,
       base_paramer.aux.resolution.vdisplay,
       base_paramer.aux.resolution.hsync_start,
       base_paramer.aux.resolution.hsync_end,
       base_paramer.aux.resolution.htotal,
       base_paramer.aux.resolution.flags,
       foundAuxIdx);
     */
}

static void hw_output_hotplug_update(struct hw_output_device* dev){
    hw_output_private_t* priv = (hw_output_private_t*)dev;

    DrmConnector *mextend = NULL;
    DrmConnector *mprimary = NULL;

    for (auto &conn : priv->drm_->connectors()) {
        drmModeConnection old_state = conn->state();

        conn->UpdateModes();

        drmModeConnection cur_state = conn->state();
        ALOGD("old_state %d cur_state %d conn->get_type() %d", old_state, cur_state, conn->get_type());

        if (cur_state == old_state)
            continue;
        ALOGI("%s event  for connector %u\n",
                cur_state == DRM_MODE_CONNECTED ? "Plug" : "Unplug", conn->id());

        if (cur_state == DRM_MODE_CONNECTED) {
            if (conn->possible_displays() & HWC_DISPLAY_EXTERNAL_BIT)
                mextend = conn.get();
            else if (conn->possible_displays() & HWC_DISPLAY_PRIMARY_BIT)
                mprimary = conn.get();
        }
    }

    /*
     * status changed?
     */
    priv->drm_->DisplayChanged();

    DrmConnector *old_primary = priv->drm_->GetConnectorFromType(HWC_DISPLAY_PRIMARY);
    mprimary = mprimary ? mprimary : old_primary;
    if (!mprimary || mprimary->state() != DRM_MODE_CONNECTED) {
        mprimary = NULL;
        for (auto &conn : priv->drm_->connectors()) {
            if (!(conn->possible_displays() & HWC_DISPLAY_PRIMARY_BIT))
                continue;
            if (conn->state() == DRM_MODE_CONNECTED) {
                mprimary = conn.get();
                break;
            }
        }
    }

    if (!mprimary) {
        ALOGE("%s %d Failed to find primary display\n", __FUNCTION__, __LINE__);
        return;
    }
    if (mprimary != old_primary) {
        priv->drm_->SetPrimaryDisplay(mprimary);
    }

    DrmConnector *old_extend = priv->drm_->GetConnectorFromType(HWC_DISPLAY_EXTERNAL);
    mextend = mextend ? mextend : old_extend;
    if (!mextend || mextend->state() != DRM_MODE_CONNECTED) {
        mextend = NULL;
        for (auto &conn : priv->drm_->connectors()) {
            if (!(conn->possible_displays() & HWC_DISPLAY_EXTERNAL_BIT))
                continue;
            if (conn->id() == mprimary->id())
                continue;
            if (conn->state() == DRM_MODE_CONNECTED) {
                mextend = conn.get();
                break;
            }
        }
    }
    priv->drm_->SetExtendDisplay(mextend);
    priv->drm_->DisplayChanged();
    priv->drm_->UpdateDisplayRoute();
    priv->drm_->ClearDisplay();

    updateConnectors(priv);
}

static int hw_output_initialize(struct hw_output_device* dev, void* data)
{
    hw_output_private_t* priv = (hw_output_private_t*)dev;

    priv->drm_ = NULL;
    priv->primary = NULL;
    priv->extend = NULL;
    priv->mlut = NULL;
    priv->callback_data = data;

    if (priv->drm_ == NULL) {
        priv->drm_ = new DrmResources();
        priv->drm_->Init();
        ALOGD("nativeInit: ");
        hw_output_hotplug_update(dev);
        if (priv->primary == NULL) {
            for (auto &conn : priv->drm_->connectors()) {
                if ((conn->possible_displays() & HWC_DISPLAY_PRIMARY_BIT)) {
                    priv->drm_->SetPrimaryDisplay(conn.get());
                    priv->primary = conn.get();
                }
                if ((conn->possible_displays() & HWC_DISPLAY_EXTERNAL_BIT) && conn->state() == DRM_MODE_CONNECTED) {
                    priv->drm_->SetExtendDisplay(conn.get());
                    priv->extend = conn.get();
                }
            }
        }
        ALOGD("primary: %p extend: %p ", priv->primary, priv->extend);
    }
    return 0;
}


/*****************************************************************************/

static int hw_output_set_mode(struct hw_output_device*, int dpy, const char* mode)
{
    char property[PROPERTY_VALUE_MAX];

    if (dpy == HWC_DISPLAY_PRIMARY){
        property_get("persist.vendor.resolution.main", property, NULL);
    } else {
        property_get("persist.vendor.resolution.aux", property, NULL);
    }
    ALOGD("nativeSetMode %s display %d", mode, dpy);

    if (strcmp(mode, property) !=0) {
        if (dpy == HWC_DISPLAY_PRIMARY)
            property_set("persist.vendor.resolution.main", mode);
        else 
            property_set("persist.vendor.resolution.aux", mode);

        int timeline = property_get_int32("vendor.display.timeline", 1);
        timeline++;
        char tmp[128];
        snprintf(tmp,128,"%d",timeline);
        property_set("vendor.display.timeline", tmp);
    }
    return 0;
}

static int hw_output_set_3d_mode(struct hw_output_device*, const char* mode)
{
    char property[PROPERTY_VALUE_MAX];
    property_get("vendor.3d_resolution.main", property, "null");
    if (strcmp(mode, property) !=0) {
        property_set("vendor.3d_resolution.main", mode);

        int timeline = property_get_int32("vendor.display.timeline", 1);
        timeline++;
        char tmp[128];
        snprintf(tmp,128,"%d",timeline);
        property_set("vendor.display.timeline", tmp);
        ALOGD("%s: setprop vendor.3d_resolution.main %s,timeline = %s",__FUNCTION__,mode,tmp);
    }
    return 0;
}

static int hw_output_set_gamma(struct hw_output_device* dev, int dpy, uint32_t size, uint16_t* r, uint16_t* g, uint16_t* b)
{
    hw_output_private_t* priv = (hw_output_private_t*)dev;
    int ret = -1;
    int crtc_id = 0;

    if (dpy == HWC_DISPLAY_PRIMARY && priv->primary)
        crtc_id = priv->drm_->GetCrtcFromConnector(priv->primary)->id();
    else if (priv->extend)
        crtc_id = priv->drm_->GetCrtcFromConnector(priv->extend)->id();
    ret = drmModeCrtcSetGamma(priv->drm_->fd(), crtc_id, size, r, g, b);
    if (ret < 0)
        ALOGE("fail to SetGamma %d(%s)", ret, strerror(errno));
    return ret;
}

static int hw_output_set_brightness(struct hw_output_device*, int dpy, int brightness)
{
    char property[PROPERTY_VALUE_MAX];
    char tmp[128];

    sprintf(tmp, "%d", brightness);
    if (dpy == HWC_DISPLAY_PRIMARY){
        property_get("persist.vendor.brightness.main", property, "50");
    } else {
        property_get("persist.vendor.brightness.aux", property, "50");
    }

    if (atoi(property) != brightness) {
        if (dpy == HWC_DISPLAY_PRIMARY)
            property_set("persist.vendor.brightness.main", tmp);
        else
            property_set("persist.vendor.brightness.aux", tmp);

        int timeline = property_get_int32("vendor.display.timeline", 1);
        timeline++;
        snprintf(tmp,128,"%d",timeline);
        property_set("vendor.display.timeline", tmp);
    }
    return 0;
}

static int hw_output_set_contrast(struct hw_output_device*, int dpy, int contrast)
{
    char property[PROPERTY_VALUE_MAX];
    char tmp[128];

    sprintf(tmp, "%d", contrast);
    if (dpy == HWC_DISPLAY_PRIMARY){
        property_get("persist.vendor.contrast.main", property, "50");
    } else {
        property_get("persist.vendor.contrast.aux", property, "50");
    }

    if (atoi(property) != contrast) {
        if (dpy == HWC_DISPLAY_PRIMARY)
            property_set("persist.vendor.contrast.main", tmp);
        else
            property_set("persist.vendor.contrast.aux", tmp);

        int timeline = property_get_int32("vendor.display.timeline", 1);
        timeline++;
        snprintf(tmp,128,"%d",timeline);
        property_set("vendor.display.timeline", tmp);
    }
    return 0;
}

static int hw_output_set_sat(struct hw_output_device*, int dpy, int sat)
{
    char property[PROPERTY_VALUE_MAX];
    char tmp[128];

    sprintf(tmp, "%d", sat);
    if (dpy == HWC_DISPLAY_PRIMARY){
        property_get("persist.vendor.saturation.main", property, "50");
    } else {
        property_get("persist.vendor.saturation.aux", property, "50");
    }

    if (atoi(property) != sat) {
        if (dpy == HWC_DISPLAY_PRIMARY)
            property_set("persist.vendor.saturation.main", tmp);
        else
            property_set("persist.vendor.saturation.aux", tmp);

        int timeline = property_get_int32("vendor.display.timeline", 1);
        timeline++;
        snprintf(tmp,128,"%d",timeline);
        property_set("vendor.display.timeline", tmp);
    }
    return 0;
}

static int hw_output_set_hue(struct hw_output_device*, int dpy, int hue)
{
    char property[PROPERTY_VALUE_MAX];
    char tmp[128];

    sprintf(tmp, "%d", hue);
    if (dpy == HWC_DISPLAY_PRIMARY){
        property_get("persist.vendor.hue.main", property, "50");
    } else {
        property_get("persist.vendor.hue.aux", property, "50");
    }

    if (atoi(property) != hue) {
        if (dpy == HWC_DISPLAY_PRIMARY)
            property_set("persist.vendor.hue.main", tmp);
        else
            property_set("persist.vendor.hue.aux", tmp);

        int timeline = property_get_int32("vendor.display.timeline", 1);
        timeline++;
        snprintf(tmp,128,"%d",timeline);
        property_set("vendor.display.timeline", tmp);
    }
    return 0;
}

static int hw_output_set_screen_scale(struct hw_output_device*, int dpy, int direction, int value)
{
    char property[PROPERTY_VALUE_MAX];
    char overscan[128];
    int left,top,right,bottom;

    if(dpy == HWC_DISPLAY_PRIMARY)
        property_get("persist.vendor.overscan.main", property, "overscan 100,100,100,100");
    else
        property_get("persist.vendor.overscan.aux", property, "overscan 100,100,100,100");

    sscanf(property, "overscan %d,%d,%d,%d", &left, &top, &right, &bottom);

    if (direction == OVERSCAN_LEFT)
        left = value;
    else if (direction == OVERSCAN_TOP)
        top = value;
    else if (direction == OVERSCAN_RIGHT)
        right = value;
    else if (direction == OVERSCAN_BOTTOM)
        bottom = value;

    sprintf(overscan, "overscan %d,%d,%d,%d", left, top, right, bottom);

    if (strcmp(property, overscan) != 0) {
        char tmp[128];
        if(dpy == HWC_DISPLAY_PRIMARY)
            property_set("persist.vendor.overscan.main", overscan);
        else
            property_set("persist.vendor.overscan.aux", overscan);

        int timeline = property_get_int32("vendor.display.timeline", 1);
        timeline++;
        snprintf(tmp,128,"%d",timeline);
        property_set("vendor.display.timeline", tmp);
    }

    return 0;
}

static int hw_output_set_hdr_mode(struct hw_output_device*, int dpy, int hdr_mode)
{
    char property[PROPERTY_VALUE_MAX];
    char tmp[128];

    sprintf(tmp, "%d", hdr_mode);
    if (dpy == HWC_DISPLAY_PRIMARY){
        property_get("persist.vendor.hdr_mode.main", property, "50");
    } else {
        property_get("persist.vendor.hdr_mode.aux", property, "50");
    }

    if (atoi(property) != hdr_mode) {
        if (dpy == HWC_DISPLAY_PRIMARY)
            property_set("persist.vendor.hdr_mode.main", tmp);
        else
            property_set("persist.vendor.hdr_mode.aux", tmp);

        int timeline = property_get_int32("vendor.display.timeline", 1);
        timeline++;
        snprintf(tmp,128,"%d",timeline);
        property_set("vendor.display.timeline", tmp);
    }
    return 0;
}

static int hw_output_set_color_mode(struct hw_output_device*, int dpy, const char* color_mode)
{
    char property[PROPERTY_VALUE_MAX];

    if (dpy == HWC_DISPLAY_PRIMARY){
        property_get("persist.vendor.color.main", property, NULL);
    } else {
        property_get("persist.vendor.color.aux", property, NULL);
    }
    ALOGD("hw_output_set_color_mode %s display %d ", color_mode, dpy);

    if (strcmp(color_mode, property) !=0) {
        if (dpy == HWC_DISPLAY_PRIMARY)
            property_set("persist.vendor.color.main", color_mode);
        else
            property_set("persist.vendor.color.aux", color_mode);

        int timeline = property_get_int32("vendor.display.timeline", 1);
        timeline++;
        char tmp[128];
        snprintf(tmp,128,"%d",timeline);
        property_set("vendor.display.timeline", tmp);
    }
    return 0;
}

static int hw_output_get_cur_mode(struct hw_output_device* dev, int dpy, char* curMode)
{
    hw_output_private_t* priv = (hw_output_private_t*)dev;
    bool found=false;

    if (curMode != NULL)
        found = getResolutionInfo(priv, dpy, curMode);
    else
        return -1;

    if (!found) {
        sprintf(curMode, "%s", "Auto");
    }

    return 0;
}

static int hw_output_get_cur_color_mode(struct hw_output_device* dev, int dpy, char* curColorMode)
{
    hw_output_private_t* priv = (hw_output_private_t*)dev;
    char colorMode[PROPERTY_VALUE_MAX];
    struct file_base_paramer base_paramer;
    int len=0;
    DrmConnector* mCurConnector;

    if (dpy == HWC_DISPLAY_PRIMARY) {
        len = property_get("persist.vendor.color.main", colorMode, NULL);
    } else if (dpy == HWC_DISPLAY_EXTERNAL) {
        len = property_get("persist.vendor.color.aux", colorMode, NULL);
    }

    ALOGD("nativeGetCurCorlorMode: property=%s", colorMode);
    if (dpy == HWC_DISPLAY_PRIMARY) {
        mCurConnector = priv->primary;
    }else if (dpy == HWC_DISPLAY_EXTERNAL){
        mCurConnector = priv->extend;
    } 
    if (!len) {
        if (getBaseParameterInfo(&base_paramer) && mCurConnector != NULL) {
            int slot = 0;
            if (dpy == HWC_DISPLAY_PRIMARY)
                slot = findSuitableInfoSlot(&base_paramer.main, mCurConnector->get_type());
            else
                slot = findSuitableInfoSlot(&base_paramer.aux, mCurConnector->get_type());

            if (dpy == HWC_DISPLAY_PRIMARY) {
                if (base_paramer.main.screen_list[slot].depthc == Automatic &&
                        base_paramer.main.screen_list[slot].format == output_ycbcr_high_subsampling)
                    sprintf(colorMode, "%s", "Auto");
            } else if (dpy == HWC_DISPLAY_EXTERNAL) {
                if (base_paramer.aux.screen_list[slot].depthc == Automatic &&
                        base_paramer.aux.screen_list[slot].format == output_ycbcr_high_subsampling)
                    sprintf(colorMode, "%s", "Auto");
            }
        }
    }
    sprintf(curColorMode, "%s", colorMode);
    ALOGD("nativeGetCurCorlorMode: colorMode=%s", colorMode);
    return 0;
}

static int hw_output_get_num_connectors(struct hw_output_device* dev, int, int* numConnectors)
{
    hw_output_private_t* priv = (hw_output_private_t*)dev;

    *numConnectors = priv->drm_->connectors().size();
    return 0;
}

static int hw_output_get_connector_state(struct hw_output_device* dev, int dpy, int* state)
{
    hw_output_private_t* priv = (hw_output_private_t*)dev;
    int ret = 0;

    if (dpy == HWC_DISPLAY_PRIMARY && priv->primary)
        *state = priv->primary->state();
    else if (dpy == HWC_DISPLAY_EXTERNAL && priv->extend)
        *state = priv->extend->state();
    else
        ret = -1;

    return ret;
}

static int hw_output_get_color_configs(struct hw_output_device* dev, int dpy, int* configs)
{
    hw_output_private_t* priv = (hw_output_private_t*)dev;
    DrmConnector* mCurConnector;
    uint64_t color_capacity=0;
    uint64_t depth_capacity=0;

    if (dpy == HWC_DISPLAY_PRIMARY) {
        mCurConnector = priv->primary;
    }else if (dpy == HWC_DISPLAY_EXTERNAL){
        mCurConnector = priv->extend;
    } else {
        return -1;
    }

    if (mCurConnector != NULL) {
        if (mCurConnector->hdmi_output_mode_capacity_property().id())
            mCurConnector->hdmi_output_mode_capacity_property().value( &color_capacity);

        if (mCurConnector->hdmi_output_depth_capacity_property().id())
            mCurConnector->hdmi_output_depth_capacity_property().value(&depth_capacity);

        configs[0] = (int)color_capacity;
        configs[1] = (int)depth_capacity;
        ALOGD("nativeGetCorlorModeConfigs: corlor=%d depth=%d configs:%d %d",(int)color_capacity,(int)depth_capacity, configs[0], configs[1]);
    }
    return 0;
}

static int hw_output_get_overscan(struct hw_output_device*, int dpy, uint32_t* overscans)
{
    char property[PROPERTY_VALUE_MAX];
    int left,top,right,bottom;

    if(dpy == HWC_DISPLAY_PRIMARY)
        property_get("persist.vendor.overscan.main", property, "overscan 100,100,100,100");
    else
        property_get("persist.vendor.overscan.aux", property, "overscan 100,100,100,100");

    sscanf(property, "overscan %d,%d,%d,%d", &left, &top, &right, &bottom);
    overscans[0] = left;
    overscans[1] = top;
    overscans[2] = right;
    overscans[3] = bottom;
    return 0;
}

static int hw_output_get_bcsh(struct hw_output_device*, int dpy, uint32_t* bcshs)
{
    char mBcshProperty[PROPERTY_VALUE_MAX];
    struct file_base_paramer base_paramer;
    bool foudBaseParameter=false;

    foudBaseParameter = getBaseParameterInfo(&base_paramer);
    if (dpy == HWC_DISPLAY_PRIMARY) {
        if (property_get("persist.vendor.brightness.main", mBcshProperty, NULL) > 0)
            bcshs[0] = atoi(mBcshProperty);
        else if (foudBaseParameter)
            bcshs[0] = base_paramer.main.bcsh.brightness;
        else
            bcshs[0] = DEFAULT_BRIGHTNESS;

        memset(mBcshProperty, 0, sizeof(mBcshProperty));
        if (property_get("persist.vendor.contrast.main", mBcshProperty, NULL) > 0)
            bcshs[1] = atoi(mBcshProperty);
        else if (foudBaseParameter)
            bcshs[1] = base_paramer.main.bcsh.contrast;
        else
            bcshs[1] = DEFAULT_CONTRAST;

        memset(mBcshProperty, 0, sizeof(mBcshProperty));
        if (property_get("persist.vendor.saturation.main", mBcshProperty, NULL) > 0)
            bcshs[2] = atoi(mBcshProperty);
        else if (foudBaseParameter)
            bcshs[2] = base_paramer.main.bcsh.saturation;
        else
            bcshs[2] = DEFAULT_SATURATION;

        memset(mBcshProperty, 0, sizeof(mBcshProperty));
        if (property_get("persist.vendor.hue.main",mBcshProperty, NULL) > 0)
            bcshs[3] = atoi(mBcshProperty);
        else if (foudBaseParameter)
            bcshs[3] = base_paramer.main.bcsh.hue;
        else
            bcshs[3] = DEFAULT_HUE;
    } else if (dpy == HWC_DISPLAY_EXTERNAL){
        if (property_get("persist.vendor.brightness.aux", mBcshProperty, NULL) > 0)
            bcshs[0] = atoi(mBcshProperty);
        else if (foudBaseParameter)
            bcshs[0] = base_paramer.aux.bcsh.brightness;
        else 
            bcshs[0] = DEFAULT_BRIGHTNESS;

        memset(mBcshProperty, 0, sizeof(mBcshProperty));
        if (property_get("persist.vendor.contrast.aux", mBcshProperty, NULL) > 0)
            bcshs[1] = atoi(mBcshProperty);
        else if (foudBaseParameter)
            bcshs[1] = base_paramer.aux.bcsh.contrast;
        else
            bcshs[1] = DEFAULT_CONTRAST;

        memset(mBcshProperty, 0, sizeof(mBcshProperty));
        if (property_get("persist.vendor.saturation.aux", mBcshProperty, NULL) > 0)
            bcshs[2] = atoi(mBcshProperty);
        else if (foudBaseParameter)
            bcshs[2] = base_paramer.aux.bcsh.saturation;
        else
            bcshs[2] = DEFAULT_SATURATION;

        memset(mBcshProperty, 0, sizeof(mBcshProperty));
        if (property_get("persist.vendor.hue.aux",mBcshProperty, NULL) > 0)
            bcshs[3] = atoi(mBcshProperty);
        else if (foudBaseParameter)
            bcshs[3] = base_paramer.aux.bcsh.hue;
        else
            bcshs[3] = DEFAULT_HUE;
    }
    checkBcshInfo(bcshs);
    ALOGD("Bcsh: %d %d %d %d main.bcsh: %d %d %d %d", bcshs[0], bcshs[1], bcshs[2], bcshs[3],
            base_paramer.main.bcsh.brightness, base_paramer.main.bcsh.contrast, base_paramer.main.bcsh.saturation, base_paramer.main.bcsh.hue);
    return 0;
}

static int hw_output_get_builtin(struct hw_output_device* dev, int dpy, int* builtin)
{
    hw_output_private_t* priv = (hw_output_private_t*)dev;

    if (dpy == HWC_DISPLAY_PRIMARY && priv->primary)
        *builtin = priv->primary->get_type();
    else if (dpy == HWC_DISPLAY_EXTERNAL && priv->extend)
        *builtin = priv->extend->get_type();
    else
        *builtin = 0;

    return 0;
}

static drm_mode_t* hw_output_get_display_modes(struct hw_output_device* dev, int dpy, uint32_t* size)
{
    hw_output_private_t* priv = (hw_output_private_t*)dev;
    std::vector<DrmMode> mModes;
    DrmConnector* mCurConnector;
    drm_mode_t* drm_modes = NULL;
    int idx=0;

    *size = 0;
    if (dpy == HWC_DISPLAY_PRIMARY) {
        mCurConnector = priv->primary;
        if (priv->primary != NULL)
            mModes = priv->primary->modes();
        ALOGD("primary built_in %d", mCurConnector->built_in());
    }else if (dpy == HWC_DISPLAY_EXTERNAL){
        if (priv->extend != NULL) {
            mCurConnector = priv->extend;
            mModes = priv->extend->modes();
            ALOGD("extend : %d", priv->extend->built_in());
        } else
            return NULL;
    } else {
        return NULL;
    }

    if (mModes.size() == 0)
        return NULL;

    drm_modes = (drm_mode_t*)malloc(sizeof(drm_mode_t) * mModes.size());

    for (size_t c = 0; c < mModes.size(); ++c) {
        const DrmMode& info = mModes[c];
        float vfresh;

        if (info.flags() & DRM_MODE_FLAG_INTERLACE)
            vfresh = info.clock()*2 / (float)(info.v_total()* info.h_total()) * 1000.0f;
        else
            vfresh = info.clock()/ (float)(info.v_total()* info.h_total()) * 1000.0f;
        drm_modes[c].width = info.h_display();
        drm_modes[c].height = info.v_display();
        drm_modes[c].refreshRate = vfresh;
        drm_modes[c].clock = info.clock();
        drm_modes[c].flags = info.flags();
        drm_modes[c].interlaceFlag = info.flags()&(1<<4);
        drm_modes[c].yuvFlag = (info.flags()&(1<<24) || info.flags()&(1<<23));
        drm_modes[c].connectorId = mCurConnector->id();
        drm_modes[c].mode_type = info.type();
        drm_modes[c].idx = idx;
        drm_modes[c].hsync_start = info.h_sync_start();
        drm_modes[c].hsync_end = info.h_sync_end();
        drm_modes[c].htotal = info.h_total();
        drm_modes[c].hskew = info.h_skew();
        drm_modes[c].vsync_start = info.v_sync_start();
        drm_modes[c].vsync_end = info.v_sync_end();
        drm_modes[c].vtotal = info.v_total();
        drm_modes[c].vscan = info.v_scan();
        idx++;
        ALOGV("display%d mode[%d]  %dx%d fps %f clk %d  h_start %d h_enc %d htotal %d hskew %d",
                dpy,(int)c, info.h_display(), info.v_display(), info.v_refresh(),
                info.clock(),  info.h_sync_start(),info.h_sync_end(),
                info.h_total(), info.h_skew());
        ALOGV("vsync_start %d vsync_end %d vtotal %d vscan %d flags 0x%x",
                info.v_sync_start(), info.v_sync_end(), info.v_total(), info.v_scan(),
                info.flags());
    }
    *size = idx;
    return drm_modes;
}

/*****************************************************************************/
static int hw_output_device_close(struct hw_device_t *dev)
{
    hw_output_private_t* priv = (hw_output_private_t*)dev;

    if (priv) {
        free(priv);
    }
    return 0;
}

static int hw_output_device_open(const struct hw_module_t* module,
        const char* name, struct hw_device_t** device)
{
    int status = -EINVAL;
    if (!strcmp(name, HW_OUTPUT_DEFAULT_DEVICE)) {
        hw_output_private_t* dev = (hw_output_private_t*)malloc(sizeof(*dev));

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = HW_OUTPUT_DEVICE_API_VERSION_0_1;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = hw_output_device_close;

        dev->device.initialize = hw_output_initialize;
        dev->device.setMode = hw_output_set_mode;
        dev->device.set3DMode = hw_output_set_3d_mode;
        dev->device.setBrightness = hw_output_set_brightness;
        dev->device.setContrast = hw_output_set_contrast;
        dev->device.setSat = hw_output_set_sat;
        dev->device.setHue = hw_output_set_hue;
        dev->device.setColorMode = hw_output_set_color_mode;
        dev->device.setHdrMode = hw_output_set_hdr_mode;
        dev->device.setGamma = hw_output_set_gamma;
        dev->device.setScreenScale = hw_output_set_screen_scale;

        dev->device.getCurColorMode = hw_output_get_cur_color_mode;
        dev->device.getBcsh = hw_output_get_bcsh;
        dev->device.getBuiltIn = hw_output_get_builtin;
        dev->device.getColorConfigs = hw_output_get_color_configs;
        dev->device.getConnectorState = hw_output_get_connector_state;
        dev->device.getCurMode = hw_output_get_cur_mode;
        dev->device.getDisplayModes = hw_output_get_display_modes;
        dev->device.getNumConnectors = hw_output_get_num_connectors;
        dev->device.getOverscan = hw_output_get_overscan;

        dev->device.hotplug = hw_output_hotplug_update;
        dev->device.saveConfig = hw_output_save_config;
        *device = &dev->device.common;
        status = 0;
    }
    return status;
}
