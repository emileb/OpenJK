LOCAL_PATH := $(call my-dir)
MPDir = codemp


JK3_BASE_CFLAGS =  -O1 -DHAVE_GLES -DFINAL_BUILD -fexceptions  -Wall -Wno-write-strings -Wno-comment   -fno-caller-saves -fno-tree-vectorize -Wno-unused-but-set-variable
JK3_BASE_CPPFLAGS =  -fvisibility-inlines-hidden -Wno-invalid-offsetof 

JK3_BASE_LDLIBS = 

#Armv7
JK3_BASE_CFLAGS += -march=armv7-a -mfloat-abi=softfp
JK3_BASE_LDLIBS += -Wl,--fix-cortex-a8

JK3_BASE_C_INCLUDES :=   $(LOCAL_PATH)/lib $(TOP_DIR)/../../Serial/jni $(TOP_DIR)/SDL2/include $(TOP_DIR)/TouchControls $(TOP_DIR)/openal/include $(TOP_DIR)/TinyXML  $(LOCAL_PATH)/$(MPDir)/client $(LOCAL_PATH)/$(MPDir)/qclib $(LOCAL_PATH)/$(MPDir)/botlib $(LOCAL_PATH)/$(MPDir)/d3d  $(LOCAL_PATH)/$(MPDir)/server  $(LOCAL_PATH)/$(MPDir)/sw  $(LOCAL_PATH)/$(MPDir)/libs/freetype2/include $(LOCAL_PATH)/$(MPDir)/common  $(LOCAL_PATH)/$(MPDir)/gl
JK3_BASE_C_INCLUDES +=   $(LOCAL_PATH)/$(MPDir)/

include $(TOP_DIR)/OpenJK/Android_client.mk
include $(TOP_DIR)/OpenJK/Android_game.mk
include $(TOP_DIR)/OpenJK/Android_cgame.mk
include $(TOP_DIR)/OpenJK/Android_ui.mk
include $(TOP_DIR)/OpenJK/Android_gles.mk



