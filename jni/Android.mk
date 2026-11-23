LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := ffhook
LOCAL_SRC_FILES := ffhook.cpp
LOCAL_LDLIBS    := -llog -ldl -landroid
include $(BUILD_SHARED_LIBRARY)
