/* test_smoke.c — 冒烟测试: 验证 harness 本身可用 */
#include "test_framework.h"

TEST(smoke_basic_asserts)
{
    CHECK_EQ_INT(1 + 1, 2);
    CHECK_STREQ("hello", "hello");
    CHECK(42 > 0);
}

TEST(smoke_string_ops)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "v%d", 7);
    CHECK_STREQ(buf, "v7");
}
