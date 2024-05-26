LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := RemovePackages
LOCAL_MODULE_CLASS := APPS
LOCAL_MODULE_TAGS := optional
LOCAL_OVERRIDES_PACKAGES := \
   CarrierConfigResCommon_Sys \
   CarrierServices \
   Contacts \
   DevicePersonalizationPrebuiltPixel2020 \
   Dialer \
   EmergencyInfoGms \
   Etar \
   GmsTelecommOverlay \
   GmsTelephonyOverlay \
   GoogleDialer \
   Jelly \
   Messages \
   MmsService \
   SimAppDialog \
   Stk \
   TeleService \
   Telecom \
   TelecommResCommon_Sys \
   TelephonyProvider \
   TelephonyResCommon_Sys \
   com.android.phone.common \
   com.google.android.dialer.support \
   messaging

LOCAL_UNINSTALLABLE_MODULE := true
LOCAL_CERTIFICATE := PRESIGNED
LOCAL_SRC_FILES := /dev/null
include $(BUILD_PREBUILT)
