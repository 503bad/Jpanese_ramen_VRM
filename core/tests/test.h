// 依存なしの最小テスト枠組み
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "rvrm/math.h"

namespace rt {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry();
extern int g_failures;

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void fail(const char* file, int line, const std::string& msg) {
    ++g_failures;
    std::printf("  FAILED %s:%d: %s\n", file, line, msg.c_str());
}

inline std::string str(const rvrm::Vec3& v) {
    char b[96];
    std::snprintf(b, sizeof b, "(%.4f, %.4f, %.4f)", v.x, v.y, v.z);
    return b;
}

}  // namespace rt

#define RT_CAT2(a, b) a##b
#define RT_CAT(a, b) RT_CAT2(a, b)
#define TEST(name)                                                         \
    static void name();                                                    \
    static rt::Registrar RT_CAT(reg_, name)(#name, name);                  \
    static void name()

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) rt::fail(__FILE__, __LINE__, #cond);                  \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                              \
    do {                                                                   \
        double _a = (a), _b = (b);                                         \
        if (!(std::fabs(_a - _b) <= (eps)))                                \
            rt::fail(__FILE__, __LINE__, std::string(#a " ~= " #b "  (") + \
                                             std::to_string(_a) + " vs " + std::to_string(_b) + ")"); \
    } while (0)

#define CHECK_VEC(a, b, eps)                                               \
    do {                                                                   \
        rvrm::Vec3 _a = (a), _b = (b);                                     \
        if (rvrm::length(_a - _b) > (eps))                                 \
            rt::fail(__FILE__, __LINE__, std::string(#a " ~= " #b "  ") + rt::str(_a) + " vs " + rt::str(_b)); \
    } while (0)
