#ifndef _I2S_CHANNEL_TEST_H_
#define _I2S_CHANNEL_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Stereo to mono conversion modes
 */
typedef enum {
    CHANNEL_CONVERT_AVERAGE,    /**< Average both channels */
    CHANNEL_CONVERT_LEFT_ONLY,  /**< Use only left channel */
    CHANNEL_CONVERT_RIGHT_ONLY  /**< Use only right channel */
} channel_convert_mode_t;

/**
 * @brief Channel balance settings
 */
typedef enum {
    CHANNEL_BALANCE_CENTER,     /**< Equal volume on both channels */
    CHANNEL_BALANCE_LEFT,       /**< Higher volume on left channel */
    CHANNEL_BALANCE_RIGHT       /**< Higher volume on right channel */
} channel_balance_t;

/**
 * @brief Test mono channel configuration
 */
void test_mono_channel_config(void);

/**
 * @brief Test stereo channel configuration
 */
void test_stereo_channel_config(void);

/**
 * @brief Test stereo to mono conversion 
 */
void test_stereo_to_mono_conversion(void);

/**
 * @brief Test mono to stereo conversion
 */
void test_mono_to_stereo_conversion(void);

/**
 * @brief Test I2S channel independence
 */
void test_channel_independence(void);

/**
 * @brief Run all I2S channel configuration tests
 */
void run_i2s_channel_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* _I2S_CHANNEL_TEST_H_ */
