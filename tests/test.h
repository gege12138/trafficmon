#ifndef TRAFFICMON_TEST_H
#define TRAFFICMON_TEST_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void (*tm_test_fn)(void);

typedef struct {
    const char *name;
    tm_test_fn function;
} tm_test_case;

extern unsigned int tm_test_failures;

#define TM_EXPECT_TRUE(condition)                                                                 \
    do {                                                                                          \
        if (!(condition)) {                                                                       \
            fprintf(stderr, "%s:%d: expected true: %s\n", __FILE__, __LINE__, #condition);       \
            tm_test_failures++;                                                                   \
        }                                                                                         \
    } while (false)

#define TM_EXPECT_EQ_U64(expected, actual)                                                        \
    do {                                                                                          \
        uint64_t tm_expected_value = (uint64_t)(expected);                                        \
        uint64_t tm_actual_value = (uint64_t)(actual);                                            \
        if (tm_expected_value != tm_actual_value) {                                               \
            fprintf(stderr, "%s:%d: expected %" PRIu64 ", got %" PRIu64 "\n", __FILE__,        \
                    __LINE__, tm_expected_value, tm_actual_value);                                \
            tm_test_failures++;                                                                   \
        }                                                                                         \
    } while (false)

#define TM_EXPECT_MEMORY_EQ(expected, actual, length)                                             \
    do {                                                                                          \
        if (memcmp((expected), (actual), (length)) != 0) {                                        \
            fprintf(stderr, "%s:%d: memory differs\n", __FILE__, __LINE__);                      \
            tm_test_failures++;                                                                   \
        }                                                                                         \
    } while (false)

#endif
