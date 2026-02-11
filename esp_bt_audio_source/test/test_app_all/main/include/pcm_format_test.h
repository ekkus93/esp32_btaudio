#ifndef _PCM_FORMAT_TEST_H_
#define _PCM_FORMAT_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration for PCM format conversion
 */
typedef struct {
    uint8_t src_bit_depth;    /**< Source bit depth (16, 24, or 32) */
    uint8_t dst_bit_depth;    /**< Destination bit depth (16, 24, or 32) */
    bool is_big_endian;       /**< Endianness flag (true for big-endian) */
} pcm_convert_cfg_t;

/**
 * @brief Test 16-bit PCM format handling
 */
void test_pcm_16bit_format(void);

/**
 * @brief Test 24-bit PCM format handling
 */
void test_pcm_24bit_format(void);

/**
 * @brief Test PCM endianness handling
 */
void test_pcm_endianness(void);

/**
 * @brief Test 16-bit to 32-bit conversion
 */
void test_pcm_16bit_to_32bit(void);

/**
 * @brief Run all PCM format tests
 */
void run_pcm_format_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* _PCM_FORMAT_TEST_H_ */
