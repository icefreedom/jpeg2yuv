LOCAL_PATH := $(call my-dir)
APP_ABI := armeabi-v7a

include $(CLEAR_VARS)
LOCAL_MODULE := libmyjpeg
LOCAL_SRC_FILES := ./libmyjpeg.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
APP_CPPFLAGS += -Wno-error=format-security
LOCAL_CFLAGS += -Wno-error=format-security
#LOCAL_CFLAGS := -fno-strict-overflow -Wno-error
LOCAL_MODULE    := libjpeg2yuv
LOCAL_SRC_FILES = jpg2yuv.c jpegutils.c lav_io.c avilib.c mjpeg_logging.c 

LOCAL_LDLIBS := -llog
#LOCAL_LDLIBS += -L$(LOCAL_PATH)/  -lWeaverVideoCodec
LOCAL_STATIC_LIBRARIES := libmyjpeg
LOCAL_SHARED_LIBRARIES :=  liblog

LOCAL_C_INCLUDES:= $(LOCAL_PATH)/

include $(BUILD_SHARED_LIBRARY)
#include $(BUILD_EXECUTABLE)
#include $(BUILD_STATIC_LIBRARY)
