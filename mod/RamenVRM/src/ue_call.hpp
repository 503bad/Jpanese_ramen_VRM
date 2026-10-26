// UFunction を名前で呼ぶための小さなヘルパー。
// 引数の配置はハードコードせず、実行時に UFunction の FProperty(名前・オフセット・サイズ)を引いて組み立てる。
// サイズが想定と違う場合は呼ばずに失敗を返す(ゲーム更新で型が変わったときに壊さないため)。
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UObject.hpp>

namespace rvrm_mod {

using RC::Unreal::FName;
using RC::Unreal::FProperty;
using RC::Unreal::UFunction;
using RC::Unreal::UObject;

// UE5 の double 版の型(レイアウトは呼び出し前に FProperty のサイズで照合する)
struct FVectorD { double X = 0, Y = 0, Z = 0; };
struct FVector2DD { double X = 0, Y = 0; };
struct FQuatD { double X = 0, Y = 0, Z = 0, W = 1; };
struct alignas(16) FTransformD {
    FQuatD Rotation;
    FVectorD Translation; double pad0 = 0;
    FVectorD Scale3D{1, 1, 1}; double pad1 = 0;
};
static_assert(sizeof(FTransformD) == 96);
struct FLinearColorF { float R = 1, G = 1, B = 1, A = 1; };
struct FProcMeshTangentD { FVectorD TangentX{1, 0, 0}; bool bFlipTangentY = false; uint8_t pad[7]{}; };
static_assert(sizeof(FProcMeshTangentD) == 32);

// TArray のヘッダーだけを自前のメモリに向ける(const 参照の引数専用。呼ばれた側が解放しない場合に限る)
struct FakeTArray {
    const void* Data = nullptr;
    int32_t Num = 0;
    int32_t Max = 0;
};
static_assert(sizeof(FakeTArray) == 16);

template <class T>
FakeTArray viewOf(const std::vector<T>& v) {
    return {v.empty() ? nullptr : v.data(), static_cast<int32_t>(v.size()), static_cast<int32_t>(v.size())};
}

class Call {
public:
    // obj のクラス階層から関数を探す。見つからなければ ok() == false
    Call(UObject* obj, const wchar_t* functionName);
    bool ok() const { return fn_ != nullptr && obj_ != nullptr && error_.empty(); }
    const std::wstring& error() const { return error_; }

    // 値をそのままコピー(サイズが一致しなければエラー)
    template <class T>
    Call& set(const wchar_t* param, const T& value) {
        if (uint8_t* p = slot(param, sizeof(T))) std::memcpy(p, &value, sizeof(T));
        return *this;
    }
    // TArray 引数(要素サイズを照合)
    template <class T>
    Call& setArray(const wchar_t* param, const std::vector<T>& v) {
        if (uint8_t* p = arraySlot(param, sizeof(T))) {
            FakeTArray a = viewOf(v);
            std::memcpy(p, &a, sizeof a);
        }
        return *this;
    }
    // TArray<uint8> 等、バイト列をそのまま渡す
    Call& setBytes(const wchar_t* param, const std::vector<uint8_t>& bytes) { return setArray(param, bytes); }
    // FString(const 参照)の引数。text は呼び出しが終わるまで生きていること(終端の 0 を含めて渡す)
    Call& setString(const wchar_t* param, const std::wstring& text);
    // サイズ不明の値(FText など)を生のバイト列として受け渡す。サイズは FProperty と照合する
    bool getRaw(const wchar_t* param, std::vector<uint8_t>& out);
    Call& setRaw(const wchar_t* param, const std::vector<uint8_t>& bytes);

    bool invoke();

    template <class T>
    bool get(const wchar_t* param, T& out) {
        uint8_t* p = slot(param, sizeof(T));
        if (!p) return false;
        std::memcpy(&out, p, sizeof(T));
        return true;
    }
    template <class T>
    bool result(T& out) { return get(L"ReturnValue", out); }

private:
    uint8_t* slot(const wchar_t* param, size_t expectedSize);
    uint8_t* arraySlot(const wchar_t* param, size_t elementSize);

    UObject* obj_ = nullptr;
    UFunction* fn_ = nullptr;
    std::vector<uint8_t> buf_;
    std::wstring error_;
};

// オブジェクトのプロパティ(UPROPERTY)の値へのポインタ。型サイズを照合する
template <class T>
T* propertyPtr(UObject* obj, const wchar_t* name) {
    if (!obj) return nullptr;
    FProperty* p = obj->GetPropertyByNameInChain(name);
    if (!p || p->GetSize() != static_cast<int32_t>(sizeof(T))) return nullptr;
    return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(obj) + p->GetOffset_Internal());
}

// オブジェクト参照のプロパティ(UObject*)を読む
UObject* objectProperty(UObject* obj, const wchar_t* name);

}  // namespace rvrm_mod
