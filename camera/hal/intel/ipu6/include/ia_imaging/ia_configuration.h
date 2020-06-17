/*
 * Copyright (C) 2017-2020 Intel Corporation.
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

#ifndef _IA_CONFIGURATION_H_
#define _IA_CONFIGURATION_H_

#define IA_HISTOGRAM_SIZE 256
#define IA_RGBS_GRID_WIDTH 100
#define IA_RGBS_GRID_HEIGHT 100
#define IA_FILTER_RESPONSE_GRID_WIDTH 100
#define IA_FILTER_RESPONSE_GRID_HEIGHT 100
#define IA_DEPTH_GRID_WIDTH 16
#define IA_DEPTH_GRID_HEIGHT 12
#define IA_CMC_GAINS_MAX_NUM 4
#define IA_CCAT_STATISTICS_MAX_NUM 3
#define IA_CCAT_EXTERNAL_RGB_HISTOGRAMS_ENABLED
#define IA_CCAT_EXTERNAL_LUMINANCE_HISTOGRAM_ENABLED
#define IA_CCAT_IR_GRID_ENABLED
#define IA_CCAT_DEPTH_GRID_ENABLED
#define IA_CCAT_RGBS_GRID_ENABLED
#define IA_CCAT_HSV_GRID_ENABLED
#define IA_CCAT_LIGHT_SOURCE_ESTIMATION_ENABLED
#define IA_CCAT_LUMINANCE_GRID_ENABLED
#define IA_CCAT_LUMINANCE_MOTION_ESTIMATE_ENABLED
#define IA_CCAT_FACE_ANALYSIS_ENABLED
#define IA_CCAT_FACES_MAX_NUM 3
#define IA_CCAT_ROI_ANALYSIS_ENABLED
#define IA_CCAT_ROIS_MAX_NUM 1
#define IA_CCAT_CONVOLUTION_FILTER_GRID_ENABLED
#define IA_CCAT_EXTERNAL_SENSORS_ENABLED
#define ENABLE_RGB_IR_SENSOR
#define ENABLE_AEC
#define IA_AEC_EXPOSURES_MAX_NUM 10
#define IA_AEC_EXPOSURE_PLANS_NUM 4
#define IA_AEC_FEATURE_WEIGHT_GRID
#define IA_AEC_WEIGHT_GRID_WIDTH 32
#define IA_AEC_WEIGHT_GRID_HEIGHT 24
#define IA_AEC_FEATURE_CALIBRATION_DATA
#define IA_AEC_FEATURE_FLASH
#define IA_AEC_FLASHES_NUM 2
#define IA_AEC_FEATURE_FLICKER_DETECTION
#define IA_AEC_FEATURE_BACKLIGHT_DETECTION
#define IA_AEC_FEATURE_FACE_UTILIZATION
#define IA_AEC_FEATURE_ROI_ENABLED
#define IA_AEC_FEATURE_APERTURE_CONTROL
#define IA_AEC_FEATURE_DC_IRIS
#define IA_AEC_FEATURE_MULTIFRAME_HINT
#define IA_AEC_FEATURE_HDR
#define IA_AEC_FEATURE_MOTION_BLUR_REDUCTION
#define IA_AEC_FEATURE_SCENE_ANALYSIS
#define ENABLE_AF
#define ENABLE_AWB
#define IA_AWB_FEATURE_SKIN_TONE_CORRECTION
#define IA_AWB_FEATURE_CAM_ENABLED
#define IA_AWB_FEATURE_SENSOR_GAMUT_MAPPING_ENABLED
#define IA_AWB_FEATURE_RGB_MAX_ENABLED
#define IA_AWB_FEATURE_HO_GW_ENABLED
#define IA_AWB_FEATURE_OT_GW_ENABLED
#define ENABLE_GBCE
#define ENABLE_PA
#define ENABLE_SA
#define IA_LSC_FEATURE_LSE_ENABLED
#define IA_BRADFORD_ADAPTATION_ENABLED
#define IA_IR_WEIGHT_CALCULATION_ENABLED
#define ENABLE_TUNING
#define IA_DEFAULT_TUNING_ENABLED
#define ENABLE_DSD
#define ENABLE_LTM
#define ENABLE_DVS

#endif /* _IA_CONFIGURATION_H_ */
