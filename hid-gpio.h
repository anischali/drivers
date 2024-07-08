#ifndef __HID_GPIO_H_
#define __HID_GPIO_H_



enum gpio_polarity {
    GPIO_POLARITY_NORMAL = 0,
    GPIO_POLARITY_INVERTED,
};
typedef int8_t gpio_polarity_t;

enum gpio_bias {
    GPIO_BIAS_DISABLE = 0,
    GPIO_BIAS_AS_IS,
    GPIO_BIAS_PULL_UP,
    GPIO_BIAS_PULL_DOWN,
};
typedef int8_t gpio_bias_t;

enum gpio_direction {
    GPIO_DIRECTION_OUTPUT= 0,
    GPIO_DIRECTION_INPUT,
};
typedef int8_t gpio_direction_t;



enum hid_gpio_usage {
    HID_USAGE_GPIO                      = 0x970000U,
    HID_USAGE_GPIO_DIRECTION	        = 0x970010U,
    HID_USAGE_GPIO_DIRECTION_OUTPUT	    = 0x970011U,
    HID_USAGE_GPIO_DIRECTION_INPUT	    = 0x970012U,
    HID_USAGE_GPIO_BIAS	                = 0x970020U,
    HID_USAGE_GPIO_BIAS_DISABLE         = 0x970021U,
    HID_USAGE_GPIO_BIAS_ASIS            = 0x970022U,
    HID_USAGE_GPIO_BIAS_PULLUP          = 0x970023U,
    HID_USAGE_GPIO_BIAS_PULLDOWN        = 0x970024U,
    HID_USAGE_GPIO_POLARITY             = 0x970030U,
    HID_USAGE_GPIO_POLARITY_ACTIVE_HIGH = 0x970031U,
    HID_USAGE_GPIO_POLARITY_ACTIVE_LOW  = 0x970032U,
    HID_USAGE_GPIO_VALUE                = 0x970040U,
    HID_USAGE_GPIO_VALUE_LOW            = 0x970041U,
    HID_USAGE_GPIO_VALUE_HIGH           = 0x970042U,
};

#endif