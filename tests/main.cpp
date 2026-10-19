#include <cstdio>
#include <cstring>

#include "check.h"

// Usage: ens_tests [substring]  -- runs only tests whose name contains the substring.
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0, failed = 0;
    for (const auto& c : ens_test::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        ens_test::failures() = 0;
        std::printf("[ RUN  ] %s\n", c.name);
        c.fn();
        if (ens_test::failures() == 0) {
            ++passed;
            std::printf("[  OK  ] %s\n", c.name);
        } else {
            ++failed;
            std::printf("[ FAIL ] %s\n", c.name);
        }
    }
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
