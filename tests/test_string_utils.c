#include "../src/common/string_utils.h"
#include <stdio.h>
#include <assert.h>

// Test helper macros
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("Running test_%s...", #name); \
    test_##name(); \
    printf(" PASS\n"); \
} while(0)

TEST(static_string_equals) {
    assert(str_equals("hello", "hello"));
    assert(!str_equals("hello", "world"));
    assert(str_equals(NULL, NULL));
    assert(!str_equals("hello", NULL));
    assert(!str_equals(NULL, "hello"));
}

TEST(static_string_starts_with) {
    assert(str_starts_with("hello world", "hello"));
    assert(str_starts_with("hello", "hello"));
    assert(!str_starts_with("hello", "hello world"));
    assert(!str_starts_with("hello", "world"));
    assert(str_starts_with("test", ""));
}

TEST(static_string_ends_with) {
    assert(str_ends_with("hello world", "world"));
    assert(str_ends_with("hello", "hello"));
    assert(!str_ends_with("hello", "hello world"));
    assert(!str_ends_with("hello", "world"));
    assert(str_ends_with("test", ""));
}

TEST(static_string_concat) {
    char* result = str_concat("hello", " world");
    assert(str_equals(result, "hello world"));
    free(result);
    
    result = str_concat("", "test");
    assert(str_equals(result, "test"));
    free(result);
    
    result = str_concat("test", "");
    assert(str_equals(result, "test"));
    free(result);
}

TEST(static_string_format) {
    char* result = str_format("Number: %d, String: %s", 42, "test");
    assert(str_equals(result, "Number: 42, String: test"));
    free(result);
}

TEST(jsa_string_builder_create_and_free) {
    JsaStringBuilder* sb = jsa_string_builder_create();
    assert(sb != NULL);
    assert(jsa_string_builder_length(sb) == 0);
    assert(jsa_string_builder_is_empty(sb));
    jsa_string_builder_free(sb);
}

TEST(jsa_string_builder_append) {
    JsaStringBuilder* sb = jsa_string_builder_create();
    
    jsa_string_builder_append(sb, "hello");
    assert(jsa_string_builder_length(sb) == 5);
    assert(str_equals(jsa_string_builder_cstr(sb), "hello"));
    
    jsa_string_builder_append(sb, " world");
    assert(jsa_string_builder_length(sb) == 11);
    assert(str_equals(jsa_string_builder_cstr(sb), "hello world"));
    
    jsa_string_builder_free(sb);
}

TEST(jsa_string_builder_append_char) {
    JsaStringBuilder* sb = jsa_string_builder_create();
    
    jsa_string_builder_append_char(sb, 'h');
    jsa_string_builder_append_char(sb, 'i');
    
    assert(jsa_string_builder_length(sb) == 2);
    assert(str_equals(jsa_string_builder_cstr(sb), "hi"));
    
    jsa_string_builder_free(sb);
}

TEST(jsa_string_builder_insert) {
    JsaStringBuilder* sb = jsa_string_builder_from_string("helloworld");
    
    jsa_string_builder_insert(sb, 5, " ");
    assert(str_equals(jsa_string_builder_cstr(sb), "hello world"));
    
    jsa_string_builder_insert(sb, 0, "Say: ");
    assert(str_equals(jsa_string_builder_cstr(sb), "Say: hello world"));
    
    jsa_string_builder_insert(sb, jsa_string_builder_length(sb), "!");
    assert(str_equals(jsa_string_builder_cstr(sb), "Say: hello world!"));
    
    jsa_string_builder_free(sb);
}

TEST(jsa_string_builder_delete) {
    JsaStringBuilder* sb = jsa_string_builder_from_string("hello world");
    
    jsa_string_builder_delete(sb, 5, 6);  // Delete " world"
    assert(str_equals(jsa_string_builder_cstr(sb), "hello"));
    
    jsa_string_builder_delete(sb, 0, 2);  // Delete "he"
    assert(str_equals(jsa_string_builder_cstr(sb), "llo"));
    
    jsa_string_builder_free(sb);
}

TEST(jsa_string_builder_replace) {
    JsaStringBuilder* sb = jsa_string_builder_from_string("hello world");
    
    jsa_string_builder_replace(sb, 6, 5, "JSasta");  // Replace "world" with "JSasta"
    assert(str_equals(jsa_string_builder_cstr(sb), "hello JSasta"));
    
    jsa_string_builder_free(sb);
}

TEST(jsa_string_builder_clear) {
    JsaStringBuilder* sb = jsa_string_builder_from_string("hello world");
    assert(jsa_string_builder_length(sb) == 11);
    
    jsa_string_builder_clear(sb);
    assert(jsa_string_builder_length(sb) == 0);
    assert(jsa_string_builder_is_empty(sb));
    assert(str_equals(jsa_string_builder_cstr(sb), ""));
    
    jsa_string_builder_free(sb);
}

TEST(jsa_string_builder_append_format) {
    JsaStringBuilder* sb = jsa_string_builder_create();
    
    jsa_string_builder_append_format(sb, "Number: %d", 42);
    assert(str_equals(jsa_string_builder_cstr(sb), "Number: 42"));
    
    jsa_string_builder_append_format(sb, ", String: %s", "test");
    assert(str_equals(jsa_string_builder_cstr(sb), "Number: 42, String: test"));
    
    jsa_string_builder_free(sb);
}

TEST(jsa_string_builder_position_to_offset) {
    JsaStringBuilder* sb = jsa_string_builder_from_string("line1\nline2\nline3");
    
    // Start of first line
    assert(jsa_string_builder_position_to_offset(sb, 0, 0) == 0);
    
    // Start of second line (after "line1\n")
    assert(jsa_string_builder_position_to_offset(sb, 1, 0) == 6);
    
    // Character 2 of second line
    assert(jsa_string_builder_position_to_offset(sb, 1, 2) == 8);
    
    // Start of third line
    assert(jsa_string_builder_position_to_offset(sb, 2, 0) == 12);
    
    jsa_string_builder_free(sb);
}

TEST(jsa_string_builder_offset_to_position) {
    JsaStringBuilder* sb = jsa_string_builder_from_string("line1\nline2\nline3");
    
    size_t line, character;
    
    // Offset 0 -> (0, 0)
    assert(jsa_string_builder_offset_to_position(sb, 0, &line, &character));
    assert(line == 0 && character == 0);
    
    // Offset 6 -> (1, 0) - start of "line2"
    assert(jsa_string_builder_offset_to_position(sb, 6, &line, &character));
    assert(line == 1 && character == 0);
    
    // Offset 8 -> (1, 2) - 'n' in "line2"
    assert(jsa_string_builder_offset_to_position(sb, 8, &line, &character));
    assert(line == 1 && character == 2);
    
    jsa_string_builder_free(sb);
}

TEST(jsa_string_builder_apply_edit) {
    JsaStringBuilder* sb = jsa_string_builder_from_string("line1\nline2\nline3");
    
    // Replace "line2" with "MODIFIED"
    TextRange range = {
        .start = {.line = 1, .character = 0},
        .end = {.line = 1, .character = 5}
    };
    
    jsa_string_builder_apply_edit(sb, &range, "MODIFIED");
    assert(str_equals(jsa_string_builder_cstr(sb), "line1\nMODIFIED\nline3"));
    
    jsa_string_builder_free(sb);
}

TEST(jsa_string_builder_take) {
    JsaStringBuilder* sb = jsa_string_builder_from_string("hello");
    
    char* str = jsa_string_builder_take(sb);
    assert(str_equals(str, "hello"));
    
    // Builder should be invalidated
    assert(jsa_string_builder_length(sb) == 0);
    
    free(str);
    jsa_string_builder_free(sb);
}

int main(void) {
    printf("Running String Utils Tests\n");
    printf("==========================\n");
    
    RUN_TEST(static_string_equals);
    RUN_TEST(static_string_starts_with);
    RUN_TEST(static_string_ends_with);
    RUN_TEST(static_string_concat);
    RUN_TEST(static_string_format);
    
    RUN_TEST(jsa_string_builder_create_and_free);
    RUN_TEST(jsa_string_builder_append);
    RUN_TEST(jsa_string_builder_append_char);
    RUN_TEST(jsa_string_builder_insert);
    RUN_TEST(jsa_string_builder_delete);
    RUN_TEST(jsa_string_builder_replace);
    RUN_TEST(jsa_string_builder_clear);
    RUN_TEST(jsa_string_builder_append_format);
    
    RUN_TEST(jsa_string_builder_position_to_offset);
    RUN_TEST(jsa_string_builder_offset_to_position);
    RUN_TEST(jsa_string_builder_apply_edit);
    RUN_TEST(jsa_string_builder_take);
    
    printf("==========================\n");
    printf("All tests passed!\n");
    
    return 0;
}
