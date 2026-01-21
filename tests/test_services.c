/*
 * test_services.c - Service initialization tests
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "services/meminfo.h"
#include "common/json.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %s... ", #name); \
    tests_run++; \
    if (test_##name()) { \
        printf("PASS\n"); \
        tests_passed++; \
    } else { \
        printf("FAIL\n"); \
    } \
} while (0)

static int test_meminfo_service_exists(void) {
    return meminfo_service.name != NULL &&
           strcmp(meminfo_service.name, "meminfo") == 0 &&
           meminfo_service.ops != NULL;
}

static int test_meminfo_has_ops(void) {
    return meminfo_service.ops->init != NULL &&
           meminfo_service.ops->collect != NULL &&
           meminfo_service.ops->snapshot != NULL;
}

int main(void) {
    printf("Service Tests\n");
    printf("=============\n");
    
    TEST(meminfo_service_exists);
    TEST(meminfo_has_ops);
    
    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
