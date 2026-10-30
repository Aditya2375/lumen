#ifndef LUMEN_TEST_HARNESS_H
#define LUMEN_TEST_HARNESS_H

#include <stdio.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do {                                   \
    tests_run++;                                                \
    if (!(cond)) {                                              \
        tests_failed++;                                         \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);    \
    }                                                           \
} while (0)

#define TEST_RESULTS() do {                                     \
    if (tests_failed) {                                         \
        printf("%d/%d checks FAILED\n", tests_failed, tests_run); \
        return 1;                                               \
    }                                                           \
    printf("%d checks passed\n", tests_run);                    \
    return 0;                                                   \
} while (0)

#endif
