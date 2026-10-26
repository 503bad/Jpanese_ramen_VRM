#include "rvrm/json.h"
#include "rvrm/math.h"
#include "test.h"

using namespace rvrm;

TEST(quat_rotate_matches_matrix) {
    Quat q = normalize(Quat::fromAxisAngle({0.3f, 1, -0.2f}, 1.1f));
    Vec3 v{0.4f, -2, 3};
    CHECK_VEC(q.rotate(v), Mat3::fromQuat(q) * v, 1e-5f);
}

TEST(quat_matrix_roundtrip) {
    const Vec3 axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}, {-1, 0.2f, 0.5f}};
    const float angles[] = {0.0f, 0.5f, 1.7f, 3.1f, -2.4f};
    for (auto& a : axes)
        for (float ang : angles) {
            Quat q = Quat::fromAxisAngle(a, ang);
            Quat r = Mat3::fromQuat(q).toQuat();
            CHECK_NEAR(std::fabs(dot(q, r)), 1.0, 1e-5);
        }
}

TEST(quat_product_order) {
    // a * b は b を先に適用
    Quat a = Quat::fromAxisAngle({0, 0, 1}, 1.5707963f);
    Quat b = Quat::fromAxisAngle({1, 0, 0}, 1.5707963f);
    Vec3 v{0, 1, 0};
    CHECK_VEC((a * b).rotate(v), a.rotate(b.rotate(v)), 1e-5f);
}

TEST(from_to_rotation) {
    Vec3 f{1, 0, 0}, t{0, 0.6f, -0.8f};
    CHECK_VEC(fromToRotation(f, t).rotate(f), t, 1e-5f);
    CHECK_VEC(fromToRotation(f, -f).rotate(f), -f, 1e-5f);
    CHECK_VEC(fromToRotation(f, f).rotate({0, 1, 0}), Vec3(0, 1, 0), 1e-6f);
}

TEST(mat4_inverse_and_trs) {
    Transform x;
    x.t = {1, 2, 3};
    x.r = Quat::fromAxisAngle({1, 2, 3}, 0.7f);
    x.s = {2, 2, 2};
    Mat4 m = Mat4::fromTRS(x);
    Mat4 i = m.inverse();
    Vec3 p{0.3f, -0.5f, 9};
    CHECK_VEC(i.transformPoint(m.transformPoint(p)), p, 1e-4f);
    CHECK_NEAR(std::fabs(dot(m.rotation(), x.r)), 1.0, 1e-5);
}

TEST(json_basic) {
    json::Value v;
    std::string err;
    bool ok = json::parse(R"({"a":1.5,"b":[true,false,null],"c":{"d":"xあ😀\n"},"e":-2e2})", v, &err);
    CHECK(ok);
    CHECK_NEAR(v["a"].asNumber(), 1.5, 1e-9);
    CHECK(v["b"].size() == 3 && v["b"][0].asBool() && !v["b"][1].asBool(true) && v["b"][2].isNull());
    CHECK(v["c"]["d"].asString() == "x\xE3\x81\x82\xF0\x9F\x98\x80\n");
    CHECK_NEAR(v["e"].asNumber(), -200, 1e-9);
    CHECK(v["missing"]["deep"].isNull());
    CHECK(v["a"].asInt() == 1);
}

TEST(json_rejects_garbage) {
    json::Value v;
    CHECK(!json::parse("{\"a\":}", v));
    CHECK(!json::parse("[1,2", v));
    CHECK(!json::parse("{} x", v));
    CHECK(!json::parse("\"unterminated", v));
    std::string deep(1000, '[');
    CHECK(!json::parse(deep, v));
}
