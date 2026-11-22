/* 微测试框架: 零依赖 assert 式单测.
 * 用法: TEST(my_case) { CHECK_EQ_INT(a, b); } — 框架 main 自动收集运行.
 * 机制: TEST 把注册项放进 .pc_sim_tests 段, runner 用 __start_/__end_ 符号遍历.
 */
#ifndef PC_SIM_TEST_FRAMEWORK_H
#define PC_SIM_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*test_fn_t)(void);

/* 注册: 每个 TEST 用 constructor 自注册 (不依赖 GNU ld 的 __start_/__end_ 段符号,
 * clang/lld 与 GCC/GNU ld 均可用) */
void pc_sim_register(const char *name, test_fn_t fn);

#define TEST(name)                                   \
    static void name(void);                          \
    static void name##_register(void)                \
        __attribute__((constructor));                \
    static void name##_register(void) {              \
        pc_sim_register(#name, name);                \
    }                                                \
    static void name(void)

/* 断言宏 */
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            pc_sim_failures++;                                            \
            return;                                                       \
        }                                                                 \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                \
    do {                                                                  \
        long _a = (long)(a), _b = (long)(b);                              \
        if (_a != _b) {                                                   \
            fprintf(stderr, "  FAIL %s:%d: %s == %ld, got %ld\n",         \
                    __FILE__, __LINE__, #a, _b, _a);                      \
            pc_sim_failures++;                                            \
            return;                                                       \
        }                                                                 \
    } while (0)

#define CHECK_STREQ(a, b)                                                 \
    do {                                                                  \
        const char *_a = (a), *_b = (b);                                  \
        if (strcmp(_a, _b) != 0) {                                        \
            fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n",           \
                    __FILE__, __LINE__, _a, _b);                          \
            pc_sim_failures++;                                            \
            return;                                                       \
        }                                                                 \
    } while (0)

extern int pc_sim_failures;
extern int pc_sim_checks;

int pc_sim_run_all(void);

#endif
