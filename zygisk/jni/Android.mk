LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := ih8securelock
LOCAL_SRC_FILES  := module.cpp binder.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_CPPFLAGS += -std=c++20 -Wall -Wextra -Wshadow -Wformat=2
LOCAL_CPPFLAGS += -D_FORTIFY_SOURCE=2 \
                  -O2 \
                  -fstack-protector-strong \
                  -fno-rtti \
                  -fno-exceptions \
                  -fvisibility=hidden \
                  -fvisibility-inlines-hidden \
                  -ffunction-sections \
                  -fdata-sections

LOCAL_CFLAGS := $(LOCAL_CPPFLAGS)
LOCAL_LDFLAGS += -Wl,-z,relro \
                 -Wl,-z,now \
                 -Wl,-z,noexecstack \
                 -Wl,--gc-sections \
                 -Wl,--icf=all \
                 -Wl,--build-id=sha1 \
                 -Wl,--exclude-libs,ALL

LOCAL_LDLIBS := -llog

include $(BUILD_SHARED_LIBRARY)
