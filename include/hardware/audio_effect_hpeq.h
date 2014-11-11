/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ANDROID_EFFECT_HPEQ_H_
#define ANDROID_EFFECT_HPEQ_H_

#include <hardware/audio_effect.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef OPENSL_ES_H_
static const effect_uuid_t SL_IID_HPEQ_ =
    {0x5d7a5de0, 0x2888, 0x11e2, 0x81c1, {0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66}};
const effect_uuid_t * const SL_IID_HPEQ = &SL_IID_HPEQ_;
#endif //OPENSL_ES_H_

#define HPEQ_BAND_COUNT    (5)


#ifdef __cplusplus
}  // extern "C"
#endif

#endif /*ANDROID_EFFECT_HPEQ_H_*/
