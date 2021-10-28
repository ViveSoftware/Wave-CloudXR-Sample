# ========= Copyright 2016-2021, HTC Corporation. All rights reserved. ===========
LOCAL_PATH := $(call my-dir)

VR_SDK_LIB := $(VR_SDK_ROOT)/jni/$(TARGET_ARCH_ABI)
OBOE_SDK_LIB := $(OBOE_SDK_ROOT)/prefab/modules/oboe/libs/android.$(TARGET_ARCH_ABI)
CLOUDXR_SDK_LIB := $(CLOUDXR_SDK_ROOT)/jni/$(TARGET_ARCH_ABI)

include $(CLEAR_VARS)
LOCAL_MODULE := wvr_api
LOCAL_SRC_FILES := $(VR_SDK_LIB)/libwvr_api.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := Oboe
LOCAL_SRC_FILES := $(OBOE_SDK_LIB)/liboboe.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := CloudXRClient
LOCAL_SRC_FILES := $(CLOUDXR_SDK_LIB)/libCloudXRClient.so
include $(PREBUILT_SHARED_LIBRARY)

COMMON_INCLUDES := \
    $(LOCAL_PATH)/include \
    $(VR_SDK_ROOT)/include \
    $(CLOUDXR_SDK_ROOT)/include \
    $(OBOE_SDK_ROOT)/prefab/modules/oboe/include \
    $(LOCAL_PATH)

COMMON_FILES := \
 WaveCloudXRApp.cpp \
 jni.cpp

#USE_CONTROLLER use device controller.
#USE_CUSTOM_CONTROLLER use device emitter.

include $(CLEAR_VARS)
LOCAL_MODULE    := WaveCloudXRJNI
LOCAL_C_INCLUDES := $(COMMON_INCLUDES)
LOCAL_SRC_FILES := $(COMMON_FILES)
LOCAL_LDLIBS    := -llog -ljnigraphics -landroid -lEGL -lGLESv3
LOCAL_SHARED_LIBRARIES := wvr_api Oboe CloudXRClient
include $(BUILD_SHARED_LIBRARY)
