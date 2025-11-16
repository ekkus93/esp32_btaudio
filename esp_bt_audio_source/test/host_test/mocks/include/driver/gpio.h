// Minimal stub of driver/gpio.h for host tests
#ifndef MOCK_DRIVER_GPIO_H
#define MOCK_DRIVER_GPIO_H

typedef int gpio_num_t;
#define GPIO_NUM_NC (-1)

/* Define a few commonly-used GPIO numbers referenced in production
 * code so the host build compiles. Values are arbitrary but distinct. */
#define GPIO_NUM_26 26
#define GPIO_NUM_25 25
#define GPIO_NUM_22 22

#endif // MOCK_DRIVER_GPIO_H
