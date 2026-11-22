/* test_framework_stub.c — 测试框架实现 (constructor 自注册 + 运行器) */
#include "test_framework.h"

int pc_sim_failures = 0;
int pc_sim_checks = 0;

typedef struct pc_sim_test_entry {
    const char *name;
    test_fn_t fn;
    struct pc_sim_test_entry *next;
} pc_sim_test_entry_t;

static pc_sim_test_entry_t *pc_sim_tests_head = NULL;

void pc_sim_register(const char *name, test_fn_t fn)
{
    pc_sim_test_entry_t *e = (pc_sim_test_entry_t *)calloc(1, sizeof(*e));
    if (!e) return;
    e->name = name;
    e->fn = fn;
    e->next = pc_sim_tests_head;
    pc_sim_tests_head = e;
}

int pc_sim_run_all(void)
{
    printf("=== pc_sim tests ===\n");
    int total = 0, passed = 0;
    pc_sim_failures = 0;
    pc_sim_checks = 0;

    for (pc_sim_test_entry_t *e = pc_sim_tests_head; e; e = e->next) {
        printf("[RUN ] %s\n", e->name);
        int before = pc_sim_failures;
        e->fn();
        if (pc_sim_failures == before) {
            passed++;
            printf("[ OK ] %s\n", e->name);
        }
        total++;
    }

    printf("=== %d/%d passed, %d check failure(s) ===\n", passed, total, pc_sim_failures);
    return pc_sim_failures == 0 ? 0 : 1;
}

int main(void)
{
    if (!pc_sim_tests_head) {
        fprintf(stderr, "no tests registered\n");
        return 1;
    }
    return pc_sim_run_all();
}
