# SPDX-License-Identifier: Apache-2.0
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := libhealthd.koa2
LOCAL_SRC_FILES := healthd.cpp
LOCAL_C_INCLUDES := system/core/healthd/include
LOCAL_CFLAGS := -Wall -Wextra -Werror
include $(BUILD_STATIC_LIBRARY)
