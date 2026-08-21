#include <print>

int run_scalar_tests();
int run_bitfield_tests();
int run_protocol_tests();
int run_fileformat_tests();

int main() {
    std::println("Hello, World!");

    if (int r = run_scalar_tests(); r != 0) {
        return r;
    }
    if (int r = run_bitfield_tests(); r != 0) {
        return r;
    }
    if (int r = run_protocol_tests(); r != 0) {
        return r;
    }
    if (int r = run_fileformat_tests(); r != 0) {
        return r;
    }

    std::println("All tests passed!");
    return 0;
}
