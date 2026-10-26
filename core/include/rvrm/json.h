// VRM 拡張(extensions.VRM / VRMC_*)を読むための最小 JSON パーサ。
// glTF 本体の解析は cgltf が行い、拡張部分の生 JSON 文字列だけをここで解析する。
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rvrm::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    static Value makeBool(bool b) { Value v; v.type_ = Type::Bool; v.bool_ = b; return v; }
    static Value makeNumber(double d) { Value v; v.type_ = Type::Number; v.num_ = d; return v; }
    static Value makeString(std::string s) { Value v; v.type_ = Type::String; v.str_ = std::move(s); return v; }
    static Value makeArray() { Value v; v.type_ = Type::Array; return v; }
    static Value makeObject() { Value v; v.type_ = Type::Object; return v; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool def = false) const { return type_ == Type::Bool ? bool_ : def; }
    double asNumber(double def = 0) const { return type_ == Type::Number ? num_ : def; }
    float asFloat(float def = 0) const { return type_ == Type::Number ? static_cast<float>(num_) : def; }
    int asInt(int def = -1) const { return type_ == Type::Number ? static_cast<int>(num_) : def; }
    const std::string& asString() const;
    std::string asString(const std::string& def) const { return type_ == Type::String ? str_ : def; }

    // 配列
    size_t size() const { return type_ == Type::Array ? arr_.size() : (type_ == Type::Object ? obj_.size() : 0); }
    const Value& operator[](size_t i) const;
    const Value& operator[](int i) const { return i < 0 ? (*this)[size_t(-1)] : (*this)[static_cast<size_t>(i)]; }
    const std::vector<Value>& items() const { return arr_; }
    std::vector<Value>& items() { return arr_; }

    // オブジェクト(存在しないキーは Null を返す)
    const Value& operator[](std::string_view key) const;
    const Value& operator[](const char* key) const { return (*this)[std::string_view(key)]; }
    bool has(std::string_view key) const;
    const std::vector<std::pair<std::string, Value>>& members() const { return obj_; }
    std::vector<std::pair<std::string, Value>>& members() { return obj_; }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<Value> arr_;
    std::vector<std::pair<std::string, Value>> obj_;
};

// 失敗時は false を返し、error に理由を入れる。
bool parse(std::string_view text, Value& out, std::string* error = nullptr);

}  // namespace rvrm::json
