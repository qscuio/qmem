/*
 * test_json.c - JSON builder tests
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
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

static int test_simple_object(void) {
    char buf[256];
    json_builder_t j;
    json_init(&j, buf, sizeof(buf));
    
    json_object_start(&j);
    json_kv_int(&j, "value", 42);
    json_kv_string(&j, "name", "test");
    json_object_end(&j);
    
    /* Check output contains expected keys */
    return strstr(buf, "\"value\":42") != NULL &&
           strstr(buf, "\"name\":\"test\"") != NULL;
}

static int test_nested_object(void) {
    char buf[512];
    json_builder_t j;
    json_init(&j, buf, sizeof(buf));
    
    json_object_start(&j);
    json_key(&j, "outer");
    json_object_start(&j);
    json_kv_int(&j, "inner", 1);
    json_object_end(&j);
    json_object_end(&j);
    
    return strstr(buf, "\"outer\":{") != NULL &&
           strstr(buf, "\"inner\":1") != NULL;
}

static int test_array(void) {
    char buf[256];
    json_builder_t j;
    json_init(&j, buf, sizeof(buf));
    
    json_object_start(&j);
    json_key(&j, "nums");
    json_array_start(&j);
    json_int(&j, 1);
    json_int(&j, 2);
    json_int(&j, 3);
    json_array_end(&j);
    json_object_end(&j);
    
    return strstr(buf, "\"nums\":[1,2,3]") != NULL;
}

int main(void) {
    printf("JSON Builder Tests\n");
    printf("==================\n");
    
    TEST(simple_object);
    TEST(nested_object);
    TEST(array);
    
    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
