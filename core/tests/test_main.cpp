#include "test.h"

namespace rt {
std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
int g_failures = 0;
}  // namespace rt

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedTests = 0;
    for (const auto& t : rt::registry()) {
        if (filter && std::string(t.name).find(filter) == std::string::npos) continue;
        int before = rt::g_failures;
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        ++run;
        if (rt::g_failures != before) ++failedTests;
    }
    std::printf("\n%d tests, %d failed, %d failed checks\n", run, failedTests, rt::g_failures);
    return rt::g_failures == 0 ? 0 : 1;
}
