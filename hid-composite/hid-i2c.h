#ifndef __HID_I2C_H_
#define __HID_I2C_H_


enum hid_i2c_usage {
    HID_USAGE_I2C                                   = 0x980000U,
    HID_USAGE_I2C_SETTING_REPORT	                = 0x980010U,
    HID_USAGE_I2C_TX_REPORT                         = 0x980020U,
    HID_USAGE_I2C_RX_REPORT                         = 0x980030U,
};

#endif