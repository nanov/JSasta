#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <sys/wait.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <spawn.h>
#include "neco.h"

// Include error catalog for mapping long names to short codes
#include "../common/jsasta_errors.h"

extern char **environ;

// --- Definitions and Globals ---
#define MAX_PATH 1024
#ifndef COMPILER_PATH
#define COMPILER_PATH "build/release/jsastac"
#endif
#ifndef RUNTIME_PATH
#define RUNTIME_PATH "build/release/runtime"
#endif
#define MAX_ENTRIES_PER_DIR 256
#define READ_BUF_SIZE 4096

#define COLOR_GREEN  "\x1b[32m"
#define COLOR_RED    "\x1b[31m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_BLUE   "\x1b[34m"
#define COLOR_CYAN   "\x1b[36m"
#define COLOR_RESET  "\x1b[0m"
#define PASS_SYMBOL   "✅"
#define FAIL_SYMBOL   "❌"
#define UPDATE_SYMBOL "📝"
#define ERROR_SYMBOL  "💥"

bool g_update_fixtures = false;
bool g_verbose_mode = false;
int g_max_parallel_jobs = 1;  // Default: sequential

typedef enum { TEST_PASS, TEST_FAIL, TEST_ERROR } test_status_t;

// --- Data structures for test configuration ---
typedef enum { MODE_RUN, MODE_COMPILER } TestMode;
typedef enum { CAPTURE_ALL, CAPTURE_STDOUT, CAPTURE_STDERR, CAPTURE_ASSERT, CAPTURE_EXPECT } CaptureStream;

typedef struct {
    int line_number;
    char error_code[64];  // e.g., "TE_UNDEFINED_VARIABLE" or "TE301"
} ExpectedError;

typedef struct {
    TestMode mode;
    CaptureStream capture_stream;
    int expected_exit_code;
    bool exit_code_was_set;
    bool expect_non_zero_exit_code;
    char* summary;
    ExpectedError* expected_errors;
    int expected_error_count;
    int expected_error_capacity;
} TestConfig;

// --- Parallel test execution structures ---
typedef struct {
    char suite_display_name[256];
    char suite_path[MAX_PATH];
    char test_filename[256];
    bool is_end_marker;  // Used to signal workers to terminate
} TestJob;

typedef enum {
    REPORT_TEST_START,
    REPORT_TEST_COMPLETE,
    REPORT_SUITE_START,
    REPORT_DONE
} ReportEventType;

typedef struct {
    ReportEventType type;
    char test_name[512];
    char suite_name[256];
    test_status_t status;
    char summary[512];  // Fixed-size buffer for pipe communication
    char failure_output[1024];  // Captured assertion failure or error output
    int worker_id;  // Which worker is reporting this
} ReportEvent;

typedef struct {
    test_status_t status;
} TestResult;

// Forward declarations
void process_directory(const char* root_dir, const char* current_dir, int* success_count, int* error_count);
test_status_t run_test_case(const char* suite_display_name, const char* suite_path, const char* test_filename);

// =========================================================================
// HELPER FUNCTIONS
// =========================================================================

char* read_file_contents(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* buffer = (char*)malloc(length + 1);
    if (!buffer) { fclose(file); return NULL; }
    fread(buffer, 1, length, file);
    buffer[length] = '\0';
    fclose(file);
    return buffer;
}

int write_file_contents(const char* path, const char* content) {
    FILE* file = fopen(path, "wb");
    if (!file) {
        // We don't use perror here to keep output clean, the caller will report.
        return -1;
    }
    size_t len = strlen(content);
    if (fwrite(content, 1, len, file) != len) {
        fclose(file);
        return -1;
    }
    fclose(file);
    return 0;
}

int ensure_directory_exists(const char* path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
    }
    return 0;
}

// Read from pipe with non-blocking I/O using neco_read
char* read_from_pipe_nonblocking(int fd) {
    char buffer[READ_BUF_SIZE];
    ssize_t bytes_read;
    size_t total_len = 0;
    char* output = (char*)malloc(1);
    if (!output) return NULL;
    output[0] = '\0';

    // Set non-blocking mode
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    while (1) {
        bytes_read = neco_read(fd, buffer, sizeof(buffer));
        if (bytes_read <= 0) break;

        char* new_output = (char*)realloc(output, total_len + bytes_read + 1);
        if (!new_output) { free(output); return NULL; }
        output = new_output;
        memcpy(output + total_len, buffer, bytes_read);
        total_len += bytes_read;
    }
    output[total_len] = '\0';
    return output;
}

int execute_and_capture_streams(const char* command, char** stdout_buf, char** stderr_buf) {
    *stdout_buf = NULL;
    *stderr_buf = NULL;

    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        return -1;
    }

    // Set up file actions for posix_spawn
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);

    // Spawn process
    pid_t pid;
    char *argv[] = {"/bin/sh", "-c", (char*)command, NULL};
    int spawn_result = posix_spawn(&pid, "/bin/sh", &actions, NULL, argv, environ);

    posix_spawn_file_actions_destroy(&actions);

    if (spawn_result != 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return -1;
    }

    // Close write ends in parent
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Read outputs using neco non-blocking I/O
    *stdout_buf = read_from_pipe_nonblocking(stdout_pipe[0]);
    *stderr_buf = read_from_pipe_nonblocking(stderr_pipe[0]);

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    // Wait for child
    int status;
    while (waitpid(pid, &status, WNOHANG) == 0) {
        neco_sleep(1 * NECO_MILLISECOND);
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

void trim_trailing_whitespace(char* str) {
    if (!str) return;
    int len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

void parse_test_config(const char* source_path, TestConfig* config) {
    config->mode = MODE_RUN;
    config->capture_stream = CAPTURE_ALL;
    config->expected_exit_code = 0;
    config->exit_code_was_set = false;
    config->expect_non_zero_exit_code = false;
    config->summary = NULL;
    config->expected_errors = NULL;
    config->expected_error_count = 0;
    config->expected_error_capacity = 0;

    FILE* file = fopen(source_path, "r");
    if (!file) return;

    char line[2048];
    int line_number = 0;
    int lines_scanned = 0;
    const int max_lines_to_scan_for_config = 10;
    char pending_error_code[64] = {0};

    while (fgets(line, sizeof(line), file)) {
        line_number++;
        char* p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') continue;

        // Parse configuration (first 10 lines only)
        if (lines_scanned < max_lines_to_scan_for_config) {
            lines_scanned++;

            if (strncmp(p, "//", 2) == 0) {
                p += 2; while (isspace((unsigned char)*p)) p++;

                if (strncmp(p, "jastat:", 7) == 0) {
                    p += 7;
                    char* token = strtok(p, " \t\n,");
                    while (token) {
                        if (strncmp(token, "mode=", 5) == 0) {
                            if (strcmp(token + 5, "compiler") == 0) config->mode = MODE_COMPILER;
                        } else if (strncmp(token, "capture=", 8) == 0) {
                            if (strcmp(token + 8, "stdout") == 0) config->capture_stream = CAPTURE_STDOUT;
                            else if (strcmp(token + 8, "stderr") == 0) config->capture_stream = CAPTURE_STDERR;
                            else if (strcmp(token + 8, "assert") == 0) config->capture_stream = CAPTURE_ASSERT;
                            else if (strcmp(token + 8, "expect") == 0) config->capture_stream = CAPTURE_EXPECT;
                        } else if (strncmp(token, "expect-exit-code=", 17) == 0) {
                            char* value = token + 17;
                            if (strcmp(value, "!0") == 0) {
                                config->expect_non_zero_exit_code = true;
                            } else {
                                config->expected_exit_code = atoi(value);
                                config->exit_code_was_set = true;
                            }
                        }
                        token = strtok(NULL, " \t\n,");
                    }
                } else if (strncmp(p, "jastat-summary:", 15) == 0) {
                    p += 15;
                    while (isspace((unsigned char)*p)) p++;
                    trim_trailing_whitespace(p);
                    if (config->summary) free(config->summary);
                    config->summary = strdup(p);
                }
            }
        }

        // Parse jastat_expect comments (entire file)
        if (strncmp(p, "//", 2) == 0) {
            p += 2; while (isspace((unsigned char)*p)) p++;
            if (strncmp(p, "jastat_expect:", 14) == 0) {
                p += 14;
                while (isspace((unsigned char)*p)) p++;
                trim_trailing_whitespace(p);
                strncpy(pending_error_code, p, sizeof(pending_error_code) - 1);
            }
        } else if (pending_error_code[0]) {
            // Non-comment line after jastat_expect - record the expectation
            if (config->expected_error_count >= config->expected_error_capacity) {
                config->expected_error_capacity = config->expected_error_capacity == 0 ? 4 : config->expected_error_capacity * 2;
                config->expected_errors = realloc(config->expected_errors, config->expected_error_capacity * sizeof(ExpectedError));
            }
            config->expected_errors[config->expected_error_count].line_number = line_number;
            strncpy(config->expected_errors[config->expected_error_count].error_code, pending_error_code, sizeof(config->expected_errors[0].error_code) - 1);
            config->expected_error_count++;
            pending_error_code[0] = '\0';
        }
    }
    fclose(file);
}

// Helper function to validate expected errors against compiler output
// Returns true if all expected errors are found
bool validate_expected_errors(const char* compiler_stderr, const ExpectedError* expected, int count, char** failure_msg) {
    if (count == 0) return true;
    
    bool* found = calloc(count, sizeof(bool));
    char* stderr_copy = strdup(compiler_stderr);
    char* line = strtok(stderr_copy, "\n");
    
    while (line) {
        // Parse error line format: [error:CODE] /path/file.jsa:LINE:COL: message
        char* error_marker = strstr(line, "[error:");
        if (error_marker) {
            char* code_start = error_marker + 7;
            char* code_end = strchr(code_start, ']');
            if (code_end) {
                char error_code[64];
                int code_len = code_end - code_start;
                if (code_len < 64) {
                    strncpy(error_code, code_start, code_len);
                    error_code[code_len] = '\0';
                    
                    // Extract line number
                    char* line_info = strstr(code_end, ".jsa:");
                    if (line_info) {
                        line_info += 5;  // Skip ".jsa:"
                        int error_line = atoi(line_info);
                        
                        // Check against expected errors
                        for (int i = 0; i < count; i++) {
                            if (!found[i] && error_line == expected[i].line_number) {
                                // Match either by short code (PE211) or long name (PE_EXPECTED_IDENTIFIER_AFTER_VAR)
                                bool matches = false;
                                
                                // Direct match with short code
                                if (strcmp(error_code, expected[i].error_code) == 0) {
                                    matches = true;
                                } else {
                                    // Try to find the error in catalog by long name and match its short code
                                    for (int e = 0; e < JSASTA_ERROR_COUNT; e++) {
                                        if (strcmp(jsasta_error_table[e].long_name, expected[i].error_code) == 0) {
                                            if (strcmp(jsasta_error_table[e].code, error_code) == 0) {
                                                matches = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                
                                if (matches) {
                                    found[i] = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        line = strtok(NULL, "\n");
    }
    
    // Check if all expected errors were found
    bool all_found = true;
    for (int i = 0; i < count; i++) {
        if (!found[i]) {
            all_found = false;
            if (failure_msg) {
                char buf[512];
                snprintf(buf, sizeof(buf), "Expected error '%s' on line %d not found",
                        expected[i].error_code, expected[i].line_number);
                if (*failure_msg) {
                    char* old = *failure_msg;
                    asprintf(failure_msg, "%s\n%s", old, buf);
                    free(old);
                } else {
                    *failure_msg = strdup(buf);
                }
            }
        }
    }
    
    free(found);
    free(stderr_copy);
    return all_found;
}

// =========================================================================
// CORE TEST LOGIC
// =========================================================================

// Pure test execution function - no output, just returns status
// For CAPTURE_ASSERT mode failures, failure_output will contain the assertion message (caller must free)
test_status_t execute_test_silent(const char* suite_path, const char* test_filename, char** failure_output) {
    if (failure_output) *failure_output = NULL;
    char absolute_source_path[PATH_MAX], temp_executable_path[MAX_PATH], fixture_path[MAX_PATH], command[PATH_MAX * 2];
    char test_base_name[MAX_PATH];
    strncpy(test_base_name, test_filename, sizeof(test_base_name));
    char* dot = strrchr(test_base_name, '.'); if (dot) *dot = '\0';
    snprintf(absolute_source_path, sizeof(absolute_source_path), "%s/%s", suite_path, test_filename);
    snprintf(temp_executable_path, sizeof(temp_executable_path), "%s/%s.tmp.exe", suite_path, test_base_name);
    snprintf(fixture_path, sizeof(fixture_path), "%s/fixtures/%s.stdout", suite_path, test_base_name);
    if (realpath(absolute_source_path, command) != NULL) { strcpy(absolute_source_path, command); }

    TestConfig config;
    parse_test_config(absolute_source_path, &config);

    test_status_t status = TEST_FAIL;
    // Use -o none for compiler-only tests to skip generating output files
    const char* output_target = (config.mode == MODE_COMPILER) ? "none" : temp_executable_path;
    snprintf(command, sizeof(command), "./%s -q -o %s %s", COMPILER_PATH, output_target, absolute_source_path);
    char* compiler_stdout = NULL, *compiler_stderr = NULL;
    int compiler_exit_code = execute_and_capture_streams(command, &compiler_stdout, &compiler_stderr);

    if (g_update_fixtures) {
        char fixtures_dir_path[MAX_PATH];
        snprintf(fixtures_dir_path, sizeof(fixtures_dir_path), "%s/fixtures", suite_path);

        if (ensure_directory_exists(fixtures_dir_path) != 0) {
            status = TEST_ERROR;
        } else {
            char* content_to_write = NULL;
            if (config.mode == MODE_COMPILER) {
                content_to_write = compiler_stderr;
            } else {
                if (compiler_exit_code == 0) {
                    if (config.capture_stream == CAPTURE_ASSERT) {
                        // For assert mode, delete fixture if it exists
                        if (access(fixture_path, F_OK) == 0) {
                            remove(fixture_path);
                        }
                        status = TEST_PASS;
                    } else {
                        char* runtime_stdout = NULL, *runtime_stderr = NULL;
                        snprintf(command, sizeof(command), "%s", temp_executable_path);
                        execute_and_capture_streams(command, &runtime_stdout, &runtime_stderr);

                        if (config.capture_stream == CAPTURE_STDOUT) { asprintf(&content_to_write, "%s", runtime_stdout); }
                        else if (config.capture_stream == CAPTURE_STDERR) { asprintf(&content_to_write, "%s", runtime_stderr); }
                        else { asprintf(&content_to_write, "%s%s", runtime_stdout, runtime_stderr); }

                        free(runtime_stdout); free(runtime_stderr);
                    }
                } else {
                    status = TEST_FAIL;
                }
            }
            if (content_to_write) {
                if (write_file_contents(fixture_path, content_to_write) == 0) {
                    status = TEST_PASS;
                } else {
                    status = TEST_ERROR;
                }
                if (config.mode == MODE_RUN) free(content_to_write);
            }
        }
    } else {
        // Test mode
        if (config.mode == MODE_COMPILER) {
            // Handle CAPTURE_EXPECT mode: validate inline error expectations
            if (config.capture_stream == CAPTURE_EXPECT) {
                bool exit_code_ok = (compiler_exit_code != 0);  // Should fail to compile
                if (!exit_code_ok) {
                    status = TEST_FAIL;
                    if (failure_output) *failure_output = strdup("Compilation succeeded but errors were expected");
                } else {
                    char* validation_error = NULL;
                    if (validate_expected_errors(compiler_stderr, config.expected_errors, 
                                                config.expected_error_count, &validation_error)) {
                        status = TEST_PASS;
                    } else {
                        status = TEST_FAIL;
                        if (failure_output && validation_error) {
                            *failure_output = validation_error;
                        } else if (validation_error) {
                            free(validation_error);
                        }
                    }
                }
            } else {
                // Original MODE_COMPILER behavior with fixtures
                bool exit_code_ok;
                if (config.expect_non_zero_exit_code) { exit_code_ok = (compiler_exit_code != 0);
                } else if (config.exit_code_was_set) { exit_code_ok = (compiler_exit_code == config.expected_exit_code);
                } else { exit_code_ok = (compiler_exit_code != 0); }

                if (!exit_code_ok) { status = TEST_FAIL; }
                else {
                    char* expected_err = read_file_contents(fixture_path);
                    if (expected_err && strcmp(compiler_stderr, expected_err) == 0) { status = TEST_PASS; }
                    else { status = TEST_FAIL; }
                    if (expected_err) free(expected_err);
                }
            }
        } else {
            if (compiler_exit_code != 0) { status = TEST_FAIL; }
            else {
                char* runtime_stdout = NULL, *runtime_stderr = NULL;
                snprintf(command, sizeof(command), "%s", temp_executable_path);
                int runtime_exit_code = execute_and_capture_streams(command, &runtime_stdout, &runtime_stderr);

                bool exit_code_ok;
                if (config.expect_non_zero_exit_code) { exit_code_ok = (runtime_exit_code != 0);
                } else { int expected = config.exit_code_was_set ? config.expected_exit_code : 0; exit_code_ok = (runtime_exit_code == expected); }

                if (!exit_code_ok) {
                    status = TEST_FAIL;
                    // For CAPTURE_ASSERT mode, capture stderr for the assertion failure message
                    if (config.capture_stream == CAPTURE_ASSERT && failure_output && runtime_stderr) {
                        *failure_output = strdup(runtime_stderr);
                    }
                }
                else {
                    if (config.capture_stream == CAPTURE_ASSERT) {
                        status = TEST_PASS;
                    } else {
                        char* capture_target = NULL;
                        if (config.capture_stream == CAPTURE_STDOUT) { capture_target = strdup(runtime_stdout); }
                        else if (config.capture_stream == CAPTURE_STDERR) { capture_target = strdup(runtime_stderr); }
                        else { asprintf(&capture_target, "%s%s", runtime_stdout, runtime_stderr); }

                        if (!capture_target) { status = TEST_ERROR; }
                        else {
                            char* expected_out = read_file_contents(fixture_path);
                            if (!expected_out) { status = TEST_ERROR; }
                            else {
                                if (strcmp(capture_target, expected_out) == 0) { status = TEST_PASS; }
                                else { status = TEST_FAIL; }
                                free(expected_out);
                            }
                            free(capture_target);
                        }
                    }
                }
                free(runtime_stdout); free(runtime_stderr);
            }
        }
    }

    if (config.summary) free(config.summary);
    if (config.expected_errors) free(config.expected_errors);
    free(compiler_stdout); free(compiler_stderr);
    remove(temp_executable_path);
    return status;
}

test_status_t run_test_case(const char* suite_display_name, const char* suite_path, const char* test_filename) {
    char absolute_source_path[PATH_MAX], temp_executable_path[MAX_PATH], fixture_path[MAX_PATH], command[PATH_MAX * 2];
    char test_base_name[MAX_PATH];
    strncpy(test_base_name, test_filename, sizeof(test_base_name));
    char* dot = strrchr(test_base_name, '.'); if (dot) *dot = '\0';
    snprintf(absolute_source_path, sizeof(absolute_source_path), "%s/%s", suite_path, test_filename);
    snprintf(temp_executable_path, sizeof(temp_executable_path), "%s/%s.tmp.ll", suite_path, test_base_name);
    snprintf(fixture_path, sizeof(fixture_path), "%s/fixtures/%s.stdout", suite_path, test_base_name);
    if (realpath(absolute_source_path, command) != NULL) { strcpy(absolute_source_path, command); }

    TestConfig config;
    parse_test_config(absolute_source_path, &config);

    char test_display_name[256];
    snprintf(test_display_name, sizeof(test_display_name), "%s/%s", suite_display_name, test_base_name);
    printf("  Running: %s\n", test_display_name);
    fflush(stdout);

    test_status_t status = TEST_FAIL;
    snprintf(command, sizeof(command), "./%s -o %s %s", COMPILER_PATH, temp_executable_path, absolute_source_path);
    char* compiler_stdout = NULL, *compiler_stderr = NULL;
    int compiler_exit_code = execute_and_capture_streams(command, &compiler_stdout, &compiler_stderr);

    char* failure_reason = NULL;
    char* actual_output_for_display = NULL;
    char* expected_output_for_display = NULL;

    if (g_update_fixtures) {
        // --- UPDATE FIXTURES LOGIC ---
        char fixtures_dir_path[MAX_PATH];
        snprintf(fixtures_dir_path, sizeof(fixtures_dir_path), "%s/fixtures", suite_path);

        // =====================================================================
        // THE FIX IS HERE: Ensure the fixtures directory exists before writing.
        // =====================================================================
        if (ensure_directory_exists(fixtures_dir_path) != 0) {
            asprintf(&failure_reason, "Failed to create fixtures directory: %s", fixtures_dir_path);
            status = TEST_ERROR;
        } else {
            char* content_to_write = NULL;
            if (config.mode == MODE_COMPILER) {
                content_to_write = compiler_stderr;
            } else { // MODE_RUN
                if (compiler_exit_code == 0) {
                    // CAPTURE_ASSERT mode: delete fixture if it exists
                    if (config.capture_stream == CAPTURE_ASSERT) {
                        if (access(fixture_path, F_OK) == 0) {
                            remove(fixture_path);
                        }
                        status = TEST_PASS;
                    } else {
                        char* runtime_stdout = NULL, *runtime_stderr = NULL;
                        // Compile and link with runtime instead of using lli
                        char temp_exe[MAX_PATH];
                        snprintf(temp_exe, sizeof(temp_exe), "%s.exe", temp_executable_path);
                        snprintf(command, sizeof(command), "clang %s " RUNTIME_PATH "/jsasta_runtime.o " RUNTIME_PATH "/display.o -o %s",
                                 temp_executable_path, temp_exe);
                        char *link_stdout, *link_stderr;
                        execute_and_capture_streams(command, &link_stdout, &link_stderr);
                        free(link_stdout); free(link_stderr);
                        snprintf(command, sizeof(command), "%s", temp_exe);
                        execute_and_capture_streams(command, &runtime_stdout, &runtime_stderr);
                        remove(temp_exe);

                        if (config.capture_stream == CAPTURE_STDOUT) { asprintf(&content_to_write, "%s", runtime_stdout); }
                        else if (config.capture_stream == CAPTURE_STDERR) { asprintf(&content_to_write, "%s", runtime_stderr); }
                        else { asprintf(&content_to_write, "%s%s", runtime_stdout, runtime_stderr); }

                        free(runtime_stdout); free(runtime_stderr);
                    }
                } else {
                    asprintf(&failure_reason, "Cannot update fixture, compilation failed.");
                    actual_output_for_display = strdup(compiler_stderr);
                    status = TEST_FAIL;
                }
            }
            if (content_to_write) {
                if (write_file_contents(fixture_path, content_to_write) == 0) {
                    status = TEST_PASS;
                } else {
                    asprintf(&failure_reason, "Failed to write fixture file: %s", fixture_path);
                    status = TEST_ERROR;
                }
                // If asprintf was used, we must free it.
                if (config.mode == MODE_RUN) free(content_to_write);
            }
        }
    } else {
        // --- TEST MODE LOGIC (largely unchanged) ---
        if (config.mode == MODE_COMPILER) {
            bool exit_code_ok; char expected_code_str[50];
            if (config.expect_non_zero_exit_code) { exit_code_ok = (compiler_exit_code != 0); snprintf(expected_code_str, sizeof(expected_code_str), "non-zero");
            } else if (config.exit_code_was_set) { exit_code_ok = (compiler_exit_code == config.expected_exit_code); snprintf(expected_code_str, sizeof(expected_code_str), "%d", config.expected_exit_code);
            } else { exit_code_ok = (compiler_exit_code != 0); snprintf(expected_code_str, sizeof(expected_code_str), "non-zero"); }

            if (!exit_code_ok) { asprintf(&failure_reason, "Compiler exited with code %d, but expected %s.", compiler_exit_code, expected_code_str); status = TEST_FAIL; }
            else {
                char* expected_err = read_file_contents(fixture_path);
                if (expected_err && strcmp(compiler_stderr, expected_err) == 0) { status = TEST_PASS; }
                else { asprintf(&failure_reason, "Compiler STDERR did not match fixture."); actual_output_for_display = strdup(compiler_stderr); expected_output_for_display = strdup(expected_err ? expected_err : ""); status = TEST_FAIL; }
                if (expected_err) free(expected_err);
            }
        } else { // MODE_RUN
            if (compiler_exit_code != 0) { asprintf(&failure_reason, "Compilation failed unexpectedly (exit code %d).", compiler_exit_code); actual_output_for_display = strdup(compiler_stderr); status = TEST_FAIL; }
            else {
                char* runtime_stdout = NULL, *runtime_stderr = NULL;
                // Compile and link with runtime instead of using lli
                char temp_exe[MAX_PATH];
                snprintf(temp_exe, sizeof(temp_exe), "%s.exe", temp_executable_path);
                snprintf(command, sizeof(command), "clang %s " RUNTIME_PATH "/jsasta_runtime.o " RUNTIME_PATH "/display.o -o %s",
                         temp_executable_path, temp_exe);
                char *link_stdout, *link_stderr;
                execute_and_capture_streams(command, &link_stdout, &link_stderr);
                free(link_stdout); free(link_stderr);
                snprintf(command, sizeof(command), "%s", temp_exe);
                int runtime_exit_code = execute_and_capture_streams(command, &runtime_stdout, &runtime_stderr);
                remove(temp_exe);

                bool exit_code_ok; char expected_code_str[50];
                if (config.expect_non_zero_exit_code) { exit_code_ok = (runtime_exit_code != 0); snprintf(expected_code_str, sizeof(expected_code_str), "non-zero");
                } else { int expected = config.exit_code_was_set ? config.expected_exit_code : 0; exit_code_ok = (runtime_exit_code == expected); snprintf(expected_code_str, sizeof(expected_code_str), "%d", expected); }

                if (!exit_code_ok) {
                    // For CAPTURE_ASSERT mode, capture stderr to show assertion failure
                    if (config.capture_stream == CAPTURE_ASSERT) {
                        asprintf(&failure_reason, "Assertion failed");
                        actual_output_for_display = strdup(runtime_stderr);
                    } else {
                        asprintf(&failure_reason, "Program exited with code %d, but expected %s.", runtime_exit_code, expected_code_str);
                    }
                    status = TEST_FAIL;
                }
                else {
                    // CAPTURE_ASSERT mode: only check exit code, don't compare output
                    if (config.capture_stream == CAPTURE_ASSERT) {
                        status = TEST_PASS;
                    } else {
                        char* capture_target = NULL;
                        if (config.capture_stream == CAPTURE_STDOUT) { capture_target = strdup(runtime_stdout); }
                        else if (config.capture_stream == CAPTURE_STDERR) { capture_target = strdup(runtime_stderr); }
                        else { asprintf(&capture_target, "%s%s", runtime_stdout, runtime_stderr); }

                        if (!capture_target) { asprintf(&failure_reason, "Memory allocation failed for capture buffer."); status = TEST_ERROR; }
                        else {
                            char* expected_out = read_file_contents(fixture_path);
                            if (!expected_out) { asprintf(&failure_reason, "Fixture not found: %s", fixture_path); status = TEST_ERROR; }
                            else {
                                if (strcmp(capture_target, expected_out) == 0) { status = TEST_PASS; }
                                else { asprintf(&failure_reason, "Program output (%s) did not match fixture.", config.capture_stream == CAPTURE_STDOUT ? "STDOUT" : (config.capture_stream == CAPTURE_STDERR ? "STDERR" : "ALL")); actual_output_for_display = strdup(capture_target); expected_output_for_display = strdup(expected_out); status = TEST_FAIL; }
                                free(expected_out);
                            }
                            free(capture_target);
                        }
                    }
                }
                free(runtime_stdout); free(runtime_stderr);
            }
        }
    }

    // --- Final Output ---
    const char* result_symbol = status == TEST_PASS ? COLOR_GREEN "[ ✅ ]" COLOR_RESET : COLOR_RED "[ ❌ ]" COLOR_RESET;
    if (g_update_fixtures && status == TEST_PASS) { result_symbol = COLOR_YELLOW "[ 📝 UPDATED ]" COLOR_RESET; }
    if (status == TEST_ERROR) { result_symbol = COLOR_RED "[ " ERROR_SYMBOL " ERROR ]" COLOR_RESET; }

    // Print the result with proper indentation
    printf("    Result: %s\n", result_symbol);
    if (config.summary) { printf(COLOR_CYAN "     ↳ %s\n" COLOR_RESET, config.summary); }
    if (failure_reason) {
        fprintf(stderr, "      FAIL: %s\n", failure_reason);
        if (expected_output_for_display && actual_output_for_display) {
            fprintf(stderr, "      Expected: '%s'\n", expected_output_for_display);
            fprintf(stderr, "      Actual:   '%s'\n", actual_output_for_display);
        } else if (actual_output_for_display) {
            // For assert mode, display the assertion failure in red
            fprintf(stderr, COLOR_RED "      %s" COLOR_RESET, actual_output_for_display);
        }
    }

    // --- Cleanup ---
    if (config.summary) free(config.summary);
    free(compiler_stdout); free(compiler_stderr);
    free(failure_reason); free(actual_output_for_display); free(expected_output_for_display);
    remove(temp_executable_path);
    return status;
}

// =========================================================================
// PARALLEL TEST EXECUTION WITH NECO
// =========================================================================

// Pure neco channels - no pthreads needed since all I/O is non-blocking
typedef struct {
    char test_name[512];
    bool active;
    int spinner_frame;
} WorkerState;

typedef struct {
    neco_chan *report_chan;
    int *success_count;
    int *error_count;
} ReporterContext;

// Reporting coroutine - handles all output synchronization
void reporter(int argc, void *argv[]) {
    (void)argc;
    // Copy arguments immediately (as per neco docs)
    neco_chan *report_chan = *(neco_chan**)argv[0];
    int *success_count = (int*)argv[1];
    int *error_count = (int*)argv[2];

    const char* spinner_frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    const int spinner_count = 10;

    // Track worker states
    WorkerState *workers = calloc(g_max_parallel_jobs, sizeof(WorkerState));
    int workers_reserved_lines = 0;  // How many lines we've reserved on screen

    while (1) {
        ReportEvent event;
        int rc = neco_chan_recv_dl(report_chan, &event, neco_now() + 100 * NECO_MILLISECOND);

        // Timeout - update spinners
        if (rc == NECO_TIMEDOUT) {
            if (workers_reserved_lines > 0) {
                // Update spinner frames
                for (int i = 0; i < g_max_parallel_jobs; i++) {
                    if (workers[i].active) {
                        workers[i].spinner_frame++;
                    }
                }

                // Redraw worker lines
                printf("\033[%dA", workers_reserved_lines);  // Move cursor up
                for (int i = 0; i < g_max_parallel_jobs; i++) {
                    printf("\033[2K");  // Clear line
                    if (workers[i].active) {
                        printf("  %s Running: %s",
                               spinner_frames[workers[i].spinner_frame % spinner_count],
                               workers[i].test_name);
                    }
                    printf("\n");
                }
                fflush(stdout);
            }
            continue;
        }

        if (rc != NECO_OK) {
            break; // Channel closed or error
        }

        switch (event.type) {
            case REPORT_SUITE_START:
                printf("\nRunning Suite: %s\n", event.suite_name);
                fflush(stdout);
                break;

            case REPORT_TEST_START: {
                int worker_id = event.worker_id;

                // Reserve lines if first time
                if (workers_reserved_lines == 0) {
                    for (int i = 0; i < g_max_parallel_jobs; i++) {
                        printf("\n");  // Reserve lines
                    }
                    workers_reserved_lines = g_max_parallel_jobs;
                }

                // Update worker state
                workers[worker_id].active = true;
                workers[worker_id].spinner_frame = 0;
                strncpy(workers[worker_id].test_name, event.test_name, sizeof(workers[worker_id].test_name) - 1);

                // Redraw all worker lines
                printf("\033[%dA", workers_reserved_lines);  // Move cursor up
                for (int i = 0; i < g_max_parallel_jobs; i++) {
                    printf("\033[2K");  // Clear line
                    if (workers[i].active) {
                        printf("  %s Running: %s",
                               spinner_frames[workers[i].spinner_frame % spinner_count],
                               workers[i].test_name);
                    }
                    printf("\n");
                }
                fflush(stdout);
                break;
            }

            case REPORT_TEST_COMPLETE: {
                int worker_id = event.worker_id;
                workers[worker_id].active = false;

                const char* result_symbol = event.status == TEST_PASS ?
                    COLOR_GREEN "✅" COLOR_RESET :
                    (event.status == TEST_ERROR ? COLOR_RED "💥" COLOR_RESET : COLOR_RED "❌" COLOR_RESET);

                if (g_update_fixtures && event.status == TEST_PASS) {
                    result_symbol = COLOR_YELLOW "📝" COLOR_RESET;
                }

                // Move cursor up to start of worker area
                printf("\033[%dA", workers_reserved_lines);

                // Clear the first worker line and print completed test
                printf("\033[2K");  // Clear line
                printf("  %s %s\n", event.test_name, result_symbol);
                if (event.summary[0] != '\0') {
                    printf("\033[2K");  // Clear line for summary
                    if (event.failure_output[0] != '\0') {
                        // Inline the failure output with the summary
                        printf(COLOR_CYAN "     ↳ %s" COLOR_RESET " → " COLOR_RED "%s\n" COLOR_RESET,
                               event.summary, event.failure_output);
                    } else {
                        printf(COLOR_CYAN "     ↳ %s\n" COLOR_RESET, event.summary);
                    }
                }

                // Redraw worker lines
                for (int i = 0; i < g_max_parallel_jobs; i++) {
                    printf("\033[2K");  // Clear line
                    if (workers[i].active) {
                        printf("  %s Running: %s\n",
                               spinner_frames[workers[i].spinner_frame % spinner_count],
                               workers[i].test_name);
                    } else {
                        printf("\n");  // Empty line for inactive worker
                    }
                }

                if (event.status == TEST_PASS) {
                    (*success_count)++;
                } else {
                    (*error_count)++;
                }

                fflush(stdout);
                break;
            }

            case REPORT_DONE:
                // Clear worker lines
                if (workers_reserved_lines > 0) {
                    printf("\033[%dA", workers_reserved_lines);
                    for (int i = 0; i < workers_reserved_lines; i++) {
                        printf("\033[2K\n");
                    }
                    printf("\033[%dA", workers_reserved_lines);
                }
                free(workers);
                neco_chan_release(report_chan);
                return;
        }
    }
    free(workers);
    neco_chan_release(report_chan);
}

// Worker coroutine that processes test jobs
void test_worker(int argc, void *argv[]) {
    (void)argc;
    // Copy arguments immediately (as per neco docs)
    neco_chan *job_chan = *(neco_chan**)argv[0];
    neco_chan *report_chan = *(neco_chan**)argv[1];
    neco_chan *done_chan = *(neco_chan**)argv[2];
    int worker_id = *(int*)argv[3];

    while (1) {
        TestJob job;
        int rc = neco_chan_recv(job_chan, &job);
        if (rc != NECO_OK) {
            neco_chan_release(job_chan);
            neco_chan_release(report_chan);
            return;
        }

        // Check for termination marker
        if (job.is_end_marker) {
            int done_signal = 1;
            neco_chan_send(done_chan, &done_signal);
            neco_chan_release(job_chan);
            neco_chan_release(report_chan);
            neco_chan_release(done_chan);
            return;
        }

        // Build absolute source path to extract config
        char absolute_source_path[PATH_MAX];
        char test_base_name[MAX_PATH];
        strncpy(test_base_name, job.test_filename, sizeof(test_base_name));
        char* dot = strrchr(test_base_name, '.');
        if (dot) *dot = '\0';

        snprintf(absolute_source_path, sizeof(absolute_source_path), "%s/%s", job.suite_path, job.test_filename);

        // Parse test config to get summary
        TestConfig config;
        parse_test_config(absolute_source_path, &config);

        // Build test display name
        char test_display_name[512];
        snprintf(test_display_name, sizeof(test_display_name), "%s/%s",
                 job.suite_display_name, test_base_name);

        // Report test start
        ReportEvent start_event = {
            .type = REPORT_TEST_START,
            .worker_id = worker_id,
        };
        start_event.summary[0] = '\0';
        strncpy(start_event.test_name, test_display_name, sizeof(start_event.test_name) - 1);
        neco_chan_send(report_chan, &start_event);

        // Execute the test silently (no output)
        char* failure_output = NULL;
        test_status_t status = execute_test_silent(job.suite_path, job.test_filename, &failure_output);

        // Report test completion
        ReportEvent complete_event = {
            .type = REPORT_TEST_COMPLETE,
            .status = status,
            .worker_id = worker_id,
        };
        if (config.summary) {
            strncpy(complete_event.summary, config.summary, sizeof(complete_event.summary) - 1);
            complete_event.summary[sizeof(complete_event.summary) - 1] = '\0';
            free(config.summary);
        } else {
            complete_event.summary[0] = '\0';
        }

        // For failed assertions, populate the failure_output field
        if (status == TEST_FAIL && failure_output && config.capture_stream == CAPTURE_ASSERT) {
            strncpy(complete_event.failure_output, failure_output, sizeof(complete_event.failure_output) - 1);
            complete_event.failure_output[sizeof(complete_event.failure_output) - 1] = '\0';
            free(failure_output);
        } else {
            complete_event.failure_output[0] = '\0';
        }
        strncpy(complete_event.test_name, test_display_name, sizeof(complete_event.test_name) - 1);
        neco_chan_send(report_chan, &complete_event);
    }
}



// =========================================================================
// DIRECTORY TRAVERSAL AND MAIN
// =========================================================================

void process_directory(const char* root_dir, const char* current_dir, int* success_count, int* error_count) {
    DIR* dir = opendir(current_dir);
    if (!dir) { return; }

    char* test_files[MAX_ENTRIES_PER_DIR], *sub_dirs[MAX_ENTRIES_PER_DIR];
    int test_file_count = 0, sub_dir_count = 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".")==0 || strcmp(entry->d_name, "..")==0 || strcmp(entry->d_name, "fixtures")==0) continue;
            if (sub_dir_count < MAX_ENTRIES_PER_DIR) sub_dirs[sub_dir_count++] = strdup(entry->d_name);
        } else if (entry->d_type == DT_REG) {
            if (isdigit((unsigned char)entry->d_name[0]) && strstr(entry->d_name, ".jsa")) {
                if (test_file_count < MAX_ENTRIES_PER_DIR) test_files[test_file_count++] = strdup(entry->d_name);
            }
        }
    }
    closedir(dir);

    if (test_file_count > 0) {
        const char* suite_display_name = current_dir + strlen(root_dir);
        if (*suite_display_name == '/') suite_display_name++;

        // Always use neco worker/reporter pattern (with 1 or N workers)
        {
            // Execution with neco coroutines using channels
            neco_chan *job_chan = NULL;
            neco_chan *report_chan = NULL;
            neco_chan *done_chan = NULL;

            // Create channels (done_chan is used to signal worker completion)
            if (neco_chan_make(&job_chan, sizeof(TestJob), 100) != NECO_OK ||
                neco_chan_make(&report_chan, sizeof(ReportEvent), 100) != NECO_OK ||
                neco_chan_make(&done_chan, sizeof(int), g_max_parallel_jobs) != NECO_OK) {
                fprintf(stderr, "Error: Failed to initialize neco runtime\n");
                exit(1);
            }

            {
                // Send suite start event
                ReportEvent suite_event = {
                    .type = REPORT_SUITE_START,
                };
                suite_event.summary[0] = '\0';
                strncpy(suite_event.suite_name, *suite_display_name ? suite_display_name : ".",
                        sizeof(suite_event.suite_name) - 1);
                neco_chan_send(report_chan, &suite_event);

                // Start reporter coroutine
                neco_chan_retain(report_chan);
                neco_start(reporter, 3, &report_chan, success_count, error_count);

                // Start worker coroutines
                static int worker_ids[64];  // Static to persist across neco_start calls
                for (int i = 0; i < g_max_parallel_jobs; i++) {
                    worker_ids[i] = i;
                    neco_chan_retain(job_chan);
                    neco_chan_retain(report_chan);
                    neco_chan_retain(done_chan);
                    neco_start(test_worker, 4, &job_chan, &report_chan, &done_chan, &worker_ids[i]);
                }

                // Send all test jobs
                for (int i = 0; i < test_file_count; i++) {
                    TestJob job = { .is_end_marker = false };
                    strncpy(job.suite_display_name, suite_display_name, sizeof(job.suite_display_name) - 1);
                    strncpy(job.suite_path, current_dir, sizeof(job.suite_path) - 1);
                    strncpy(job.test_filename, test_files[i], sizeof(job.test_filename) - 1);
                    neco_chan_send(job_chan, &job);
                    free(test_files[i]);
                }

                // Send termination markers to workers
                TestJob end_marker = { .is_end_marker = true };
                for (int i = 0; i < g_max_parallel_jobs; i++) {
                    neco_chan_send(job_chan, &end_marker);
                }

                // Wait for all workers to complete by receiving done signals
                for (int i = 0; i < g_max_parallel_jobs; i++) {
                    int done_signal;
                    neco_chan_recv(done_chan, &done_signal);
                }

                // Signal reporter to finish
                ReportEvent done_event = { .type = REPORT_DONE };
                neco_chan_send(report_chan, &done_event);

                // Give reporter time to process final event
                neco_sleep(100 * NECO_MILLISECOND);

                // Clean up
                neco_chan_release(job_chan);
                neco_chan_release(report_chan);
            }
        }
    }

    for (int i = 0; i < sub_dir_count; i++) {
        char next_path[MAX_PATH];
        snprintf(next_path, sizeof(next_path), "%s/%s", current_dir, sub_dirs[i]);
        process_directory(root_dir, next_path, success_count, error_count);
        free(sub_dirs[i]);
    }
}

int get_cpu_count() {
    int count;
    size_t size = sizeof(count);
    if (sysctlbyname("hw.ncpu", &count, &size, NULL, 0) == 0) {
        return count;
    }
    return 1;  // fallback
}

void print_usage(const char* prog_name) {
    fprintf(stderr, "Usage: %s [options] <tests_directory>\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -u, --update-fixtures   Create or update fixture files.\n");
    fprintf(stderr, "  -v, --verbose           Print full compiler output for every test.\n");
    fprintf(stderr, "  -j, --jobs <N>          Run N tests in parallel (default: cores/2, use -j1 for sequential).\n");
}

// Global variables for main coroutine
static const char* g_root_tests_dir = NULL;
static int g_final_exit_code = 0;

// Main coroutine that runs the test suite
void test_runner_main(int argc, void *argv[]) {
    (void)argc;
    (void)argv;

    printf("Starting test runner in %s mode...\n", g_update_fixtures ? "UPDATE" : "TEST");
    if (g_verbose_mode) printf(COLOR_BLUE "Verbose mode enabled.\n" COLOR_RESET);

    int success_count = 0, error_count = 0;
    process_directory(g_root_tests_dir, g_root_tests_dir, &success_count, &error_count);

    printf("\n----------------------------------------\n");
    if (g_update_fixtures) {
        printf("Fixture Generation Summary:\n");
        printf(COLOR_YELLOW "  Fixtures updated: %d\n" COLOR_RESET, success_count);
        if (error_count > 0) {
             printf(COLOR_RED   "  Errors updating fixtures: %d\n" COLOR_RESET, error_count);
        }
    } else {
        printf("Test Summary:\n");
        printf(COLOR_GREEN "  Passed: %d\n" COLOR_RESET, success_count);
        printf(COLOR_RED   "  Failed: %d\n" COLOR_RESET, error_count);
        printf("  Total:  %d\n", success_count + error_count);
    }
    if (error_count > 0 && !g_update_fixtures) {
        printf(COLOR_RED "  Errors/Failures encountered: %d\n" COLOR_RESET, error_count);
    }
    printf("----------------------------------------\n");

    g_final_exit_code = (error_count > 0) ? 1 : 0;
}

int neco_main(int argc, char *argv[]) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    // Set default parallel jobs to cores/2
    g_max_parallel_jobs = get_cpu_count() / 2;
    if (g_max_parallel_jobs < 1) g_max_parallel_jobs = 1;

    const char* root_tests_dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u")==0 || strcmp(argv[i], "--update-fixtures")==0) {
            g_update_fixtures = true;
        }
        else if (strcmp(argv[i], "-v")==0 || strcmp(argv[i], "--verbose")==0) {
            g_verbose_mode = true;
        }
        else if (strcmp(argv[i], "-j")==0 || strcmp(argv[i], "--jobs")==0) {
            if (i + 1 < argc) {
                g_max_parallel_jobs = atoi(argv[++i]);
                if (g_max_parallel_jobs < 1) g_max_parallel_jobs = 1;
            }
        }
        else if (strncmp(argv[i], "-j", 2)==0 && isdigit(argv[i][2])) {
            // Handle -j1, -j2, etc. format
            g_max_parallel_jobs = atoi(argv[i] + 2);
            if (g_max_parallel_jobs < 1) g_max_parallel_jobs = 1;
        }
        else if (argv[i][0] != '-') {
            root_tests_dir = argv[i];
        }
    }

    if (!root_tests_dir) { print_usage(argv[0]); return 1; }
    if (access(COMPILER_PATH, X_OK) == -1) {
        fprintf(stderr, "Error: Compiler not found or not executable at '%s'\n", COMPILER_PATH);
        return 1;
    }

    // Set global for coroutine to access
    g_root_tests_dir = root_tests_dir;

    // Call test runner main coroutine
    test_runner_main(0, NULL);

    return g_final_exit_code;
}
