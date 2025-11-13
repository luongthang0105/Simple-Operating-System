#define COLOR_GREEN "\033[1;32m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_RED "\033[1;31m"
#define COLOR_RESET "\033[0m"

#define RUN_TEST(fn) \
    do { \
        printf("Running "); \
        printf(COLOR_YELLOW "%s... \n" COLOR_RESET, #fn); \
        fn(); \
        printf(COLOR_GREEN "Passed!\n" COLOR_RESET); \
    } while (0)