#ifndef __HID_FIRMWARE_H_
#define __HID_FIRMWARE_H_


enum _board_type {
	BOARD_TYPE_GENERIC = 0,
	BOARD_TYPE_SOC,
	BOARD_TYPE_CARRIER,
	BOARD_TYPE_BACKPLANE,
	BOARD_TYPE_EXTENTION,
};
typedef uint8_t board_type_t;

enum hid_firmware_usage {
    HID_USAGE_FIRMWARE                                    = 0x990000U,
    HID_USAGE_FIRMWARE_WAKEUP_REASON                      = 0x990010U,
    HID_USAGE_FIRMWARE_BOOTUP_REASON                      = 0x990011U,
    HID_USAGE_FIRMWARE_RESET_REASON                       = 0x990012U,
    HID_USAGE_FIRMWARE_NAME                               = 0x990013U,
    HID_USAGE_FIRMWARE_FILE_VERSION                       = 0x990014U,
    HID_USAGE_FIRMWARE_PRODUCT_VERSION                    = 0x990015U,
    HID_USAGE_FIRMWARE_APP_ID                             = 0x990016U,
    HID_USAGE_FIRMWARE_SIGNATURE                          = 0x990017U,
    HID_USAGE_FIRMWARE_VERSION                            = 0x990018U,
    HID_USAGE_FIRMWARE_HW_REVISION                        = 0x990019U,
    HID_USAGE_FIRMWARE_BOARD_TYPE                         = 0x99001AU,           
};

#endif