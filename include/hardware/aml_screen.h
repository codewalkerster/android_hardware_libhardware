/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef ANDROID_INCLUDE_HARDWARE_AML_SCREEN_H
#define ANDROID_INCLUDE_HARDWARE_AML_SCREEN_H

#include <hardware/hardware.h>
#include <android/native_window.h>


__BEGIN_DECLS

/*****************************************************************************/

/**
 * The id of this module
 */

#define AML_SCREEN_HARDWARE_MODULE_ID  "screen_source"
#define AML_SCREEN_SOURCE              "screen_source"


/*****************************************************************************/

typedef void (*olStateCB)(int state);

typedef void (*app_data_callback)(void* user,
        int *buffer);

struct aml_screen_device;

typedef struct aml_screen_module {
    struct hw_module_t common;
} aml_screen_module_t;

enum SOURCETYPE{
    WIFI_DISPLAY,
    HDMI_IN,
};

typedef struct aml_screen_operations {
    int (*start)(struct aml_screen_device*);
    int (*stop)(struct aml_screen_device*);
	int (*pause)(struct aml_screen_device*);
	int (*setStateCallBack)(struct aml_screen_device*, olStateCB);
	int (*setPreviewWindow)(struct aml_screen_device*, ANativeWindow*);
	int (*setDataCallBack)(struct aml_screen_device*,app_data_callback, void*);
    int (*get_format)(struct aml_screen_device*);
    int (*set_format)(struct aml_screen_device*, int, int, int);
    int (*set_rotation)(struct aml_screen_device*, int);
    int (*set_crop)(struct aml_screen_device*, int, int, int, int);
    int (*aquire_buffer)(struct aml_screen_device*, int*);
	// int (*set_buffer_refcount)(struct aml_screen_device, int*, int); 
    int (*release_buffer)(struct aml_screen_device*, int*);
	// int (*inc_buffer_refcount)(struct aml_screen_device*, int*);
    int (*set_frame_rate)(struct aml_screen_device*, int);
    int (*set_source_type)(struct aml_screen_device*, SOURCETYPE);
    int (*get_source_type)(struct aml_screen_device*);
} aml_screen_operations_t;

typedef struct aml_screen_device {
    hw_device_t common;
    aml_screen_operations_t ops;
    void* priv;
} aml_screen_device_t;

/*****************************************************************************/

__END_DECLS

#endif
