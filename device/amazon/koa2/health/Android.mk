# SPDX-License-Identifier: Apache-2.0
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := libhealthd.koa2
LOCAL_SRC_FILES := healthd.cpp
LOCAL_C_INCLUDES := system/core/healthd/include
# healthd.cpp pulls batteryservice/BatteryService.h -> binder/Parcel.h ->
# android-base/unique_fd.h, cutils/*, utils/*. A static library compiles its
# own sources, so these header paths must be on it directly, not just exported
# by the libraries a consumer links.
LOCAL_HEADER_LIBRARIES := \
    libbase_headers \
    libbinder_headers \
    libutils_headers \
    libcutils_headers \
    libbatteryservice_headers
LOCAL_CFLAGS := -Wall -Wextra -Werror
include $(BUILD_STATIC_LIBRARY)
