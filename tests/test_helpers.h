#ifndef MICROTHREAD_TEST_HELPERS_H
#define MICROTHREAD_TEST_HELPERS_H

#include <stdio.h>
#include <stdlib.h>

#define MT_CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        abort(); \
    } \
} while (0)

#define MT_EXPECT_EQ_INT(actual, expected) do { \
    int mt_actual__ = (actual); \
    int mt_expected__ = (expected); \
    if (mt_actual__ != mt_expected__) { \
        fprintf(stderr, "EXPECT_EQ_INT failed at %s:%d: %s == %s (actual=%d expected=%d)\n", \
                __FILE__, __LINE__, #actual, #expected, mt_actual__, mt_expected__); \
        abort(); \
    } \
} while (0)

#define MT_EXPECT_EQ_SIZE(actual, expected) do { \
    size_t mt_actual__ = (actual); \
    size_t mt_expected__ = (expected); \
    if (mt_actual__ != mt_expected__) { \
        fprintf(stderr, "EXPECT_EQ_SIZE failed at %s:%d: %s == %s (actual=%zu expected=%zu)\n", \
                __FILE__, __LINE__, #actual, #expected, mt_actual__, mt_expected__); \
        abort(); \
    } \
} while (0)

#define MT_EXPECT_OK(expr) MT_EXPECT_EQ_INT((expr), MT_OK)
#define MT_EXPECT_ERR(expr, err) MT_EXPECT_EQ_INT((expr), (err))

#ifndef CHECK
#define CHECK(cond) MT_CHECK(cond)
#endif

#endif /* MICROTHREAD_TEST_HELPERS_H */
