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
typedef enum { CAPTURE_ALL, CAPTURE_STDOUT, CAPTURE_STDERR, CAPTURE_ASSERT } CaptureStream;

typedef struct {
    TestMode mode;
    CaptureStream capture_stream;
    int expected_exit_code;
    bool exit_code_was_set;
    bool expect_non_zero_exit_code;
    char* summary;
} TestConfig;

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

char* read_from_pipe(int fd) {
    char buffer[READ_BUF_SIZE];
    ssize_t bytes_read;
    size_t total_len = 0;
    char* output = (char*)malloc(1);
    if (!output) return NULL;
    output[0] = '\0';

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
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
    int stdout_pipe[2], stderr_pipe[2];
    *stdout_buf = NULL; *stderr_buf = NULL;

    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) { perror("pipe"); return -1; }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }

    if (pid == 0) { // Child process
        close(stdout_pipe[0]); close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]); close(stderr_pipe[1]);
        execl("/bin/sh", "sh", "-c", command, (char *) NULL);
        perror("execl"); _exit(127);
    }

    // Parent process
    close(stdout_pipe[1]); close(stderr_pipe[1]);
    *stdout_buf = read_from_pipe(stdout_pipe[0]);
    *stderr_buf = read_from_pipe(stderr_pipe[0]);
    close(stdout_pipe[0]); close(stderr_pipe[0]);

    int status;
    waitpid(pid, &status, 0);

    // Handle both normal exits and signal termination
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        // Process was terminated by a signal (e.g., SIGABRT from abort())
        // Return 128 + signal number (common convention)
        return 128 + WTERMSIG(status);
    }
    return -1; // Should not reach here
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

    FILE* file = fopen(source_path, "r");
    if (!file) return;

    char line[2048];
    int lines_scanned = 0;
    const int max_lines_to_scan = 10;

    while (fgets(line, sizeof(line), file) && lines_scanned < max_lines_to_scan) {
        char* p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') continue;
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
    fclose(file);
}

// =========================================================================
// CORE TEST LOGIC
// =========================================================================

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
    printf("  %-60s", test_display_name);
    fflush(stdout);
    const char* spinner = "|/-\\"; for (int i=0; i<4; ++i) { printf("\b%c", spinner[i]); fflush(stdout); usleep(100000); } printf("\b");

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
                    // CAPTURE_ASSERT mode: no fixture to update, just mark as pass
                    if (config.capture_stream == CAPTURE_ASSERT) {
                        status = TEST_PASS;
                    } else {
                        char* runtime_stdout = NULL, *runtime_stderr = NULL;
                        // Compile and link with runtime instead of using lli
                        char temp_exe[MAX_PATH];
                        snprintf(temp_exe, sizeof(temp_exe), "%s.exe", temp_executable_path);
                        snprintf(command, sizeof(command), "clang %s " RUNTIME_PATH "/jsasta_runtime.o " RUNTIME_PATH "/display.o -o %s 2>/dev/null",
                                 temp_executable_path, temp_exe);
                        system(command);
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
                snprintf(command, sizeof(command), "clang %s " RUNTIME_PATH "/jsasta_runtime.o " RUNTIME_PATH "/display.o -o %s 2>/dev/null",
                         temp_executable_path, temp_exe);
                system(command);
                snprintf(command, sizeof(command), "%s", temp_exe);
                int runtime_exit_code = execute_and_capture_streams(command, &runtime_stdout, &runtime_stderr);
                remove(temp_exe);

                bool exit_code_ok; char expected_code_str[50];
                if (config.expect_non_zero_exit_code) { exit_code_ok = (runtime_exit_code != 0); snprintf(expected_code_str, sizeof(expected_code_str), "non-zero");
                } else { int expected = config.exit_code_was_set ? config.expected_exit_code : 0; exit_code_ok = (runtime_exit_code == expected); snprintf(expected_code_str, sizeof(expected_code_str), "%d", expected); }

                if (!exit_code_ok) { asprintf(&failure_reason, "Program exited with code %d, but expected %s.", runtime_exit_code, expected_code_str); status = TEST_FAIL; }
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

    printf("%s\n", result_symbol);
    if (config.summary) { printf(COLOR_CYAN "     ↳ %s\n" COLOR_RESET, config.summary); }
    if (failure_reason) {
        fprintf(stderr, "      FAIL: %s\n", failure_reason);
        if (expected_output_for_display && actual_output_for_display) {
            fprintf(stderr, "      Expected: '%s'\n", expected_output_for_display);
            fprintf(stderr, "      Actual:   '%s'\n", actual_output_for_display);
        } else if (actual_output_for_display) {
             fprintf(stderr, "      Output:\n---\n%s---\n", actual_output_for_display);
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
        printf("\nRunning Suite: %s\n", *suite_display_name ? suite_display_name : ".");

        if (g_max_parallel_jobs == 1) {
            // Sequential execution
            for (int i = 0; i < test_file_count; i++) {
                test_status_t status = run_test_case(suite_display_name, current_dir, test_files[i]);
                if (status == TEST_PASS) { (*success_count)++; } else { (*error_count)++; }
                free(test_files[i]);
            }
        } else {
            // Parallel execution
            int active_processes = 0;
            int next_test = 0;

            while (next_test < test_file_count || active_processes > 0) {
                // Spawn new processes up to the limit
                while (active_processes < g_max_parallel_jobs && next_test < test_file_count) {
                    pid_t pid = fork();
                    if (pid == 0) {
                        // Child process - run test and exit
                        test_status_t status = run_test_case(suite_display_name, current_dir, test_files[next_test]);
                        exit(status == TEST_PASS ? 0 : 1);
                    } else if (pid > 0) {
                        // Parent process
                        active_processes++;
                        next_test++;
                    } else {
                        // Fork failed - fall back to sequential
                        fprintf(stderr, "Warning: fork() failed, falling back to sequential execution\n");
                        for (int i = next_test; i < test_file_count; i++) {
                            test_status_t status = run_test_case(suite_display_name, current_dir, test_files[i]);
                            if (status == TEST_PASS) { (*success_count)++; } else { (*error_count)++; }
                        }
                        goto cleanup_files;
                    }
                }

                // Wait for any child to complete
                if (active_processes > 0) {
                    int status;
                    pid_t finished = wait(&status);
                    if (finished > 0) {
                        active_processes--;
                        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                            (*success_count)++;
                        } else {
                            (*error_count)++;
                        }
                    }
                }
            }

cleanup_files:
            for (int i = 0; i < test_file_count; i++) {
                free(test_files[i]);
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

int main(int argc, char* argv[]) {
	struct winsize w;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	printf("w: %zU, %d\n", TIOCGWINSZ, w.ws_col);
	return 0;
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
        else if (argv[i][0] != '-') {
            root_tests_dir = argv[i];
        }
    }

    if (!root_tests_dir) { print_usage(argv[0]); return 1; }
    if (access(COMPILER_PATH, X_OK) == -1) {
        fprintf(stderr, "Error: Compiler not found or not executable at '%s'\n", COMPILER_PATH);
        return 1;
    }

    printf("Starting test runner in %s mode...\n", g_update_fixtures ? "UPDATE" : "TEST");
    if (g_verbose_mode) printf(COLOR_BLUE "Verbose mode enabled.\n" COLOR_RESET);

    int success_count = 0, error_count = 0;
    process_directory(root_tests_dir, root_tests_dir, &success_count, &error_count);

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

    return (error_count > 0) ? 1 : 0;
}
