/* Forwarder to the canonical public header to avoid duplicate local
 * test-only definitions that conflict with production headers. This
 * keeps the test_app include path safe while reusing the single source
 * of truth in main/include/audio_processor.h
 */

#ifndef TEST_APP_AUDIO_PROCESSOR_FORWARDER_H
#define TEST_APP_AUDIO_PROCESSOR_FORWARDER_H

#include "../../../main/include/audio_processor.h"

#endif /* TEST_APP_AUDIO_PROCESSOR_FORWARDER_H */

