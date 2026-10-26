#include "ue_call.hpp"

namespace rvrm_mod {

// 引数バッファへ FName をそのままコピーするため、UE の FName と同じ 8 バイトであること
static_assert(sizeof(RC::Unreal::FName) == 8, "FName size");
// エンジンの TArray<TWeakObjectPtr<>> を直接読むため、UE と同じ 8 バイトであること
static_assert(sizeof(RC::Unreal::FWeakObjectPtr) == 8, "FWeakObjectPtr size");

using namespace RC::Unreal;

Call::Call(UObject* obj, const wchar_t* functionName) : obj_(obj) {
    if (!obj) {
        error_ = L"object is null";
        return;
    }
    fn_ = obj->GetFunctionByNameInChain(functionName);
    if (!fn_) {
        error_ = std::wstring(L"function not found: ") + functionName;
        return;
    }
    buf_.assign(fn_->GetParmsSize(), 0);
}

uint8_t* Call::slot(const wchar_t* param, size_t expectedSize) {
    if (!fn_) return nullptr;
    FProperty* p = fn_->FindProperty(FName(param, FNAME_Find));
    if (!p) {
        error_ = std::wstring(L"param not found: ") + param;
        return nullptr;
    }
    if (static_cast<size_t>(p->GetSize()) != expectedSize) {
        error_ = std::wstring(L"param size mismatch: ") + param + L" (" + std::to_wstring(p->GetSize()) + L" vs " +
                 std::to_wstring(expectedSize) + L")";
        return nullptr;
    }
    size_t off = static_cast<size_t>(p->GetOffset_Internal());
    if (off + expectedSize > buf_.size()) {
        error_ = std::wstring(L"param out of range: ") + param;
        return nullptr;
    }
    return buf_.data() + off;
}

uint8_t* Call::arraySlot(const wchar_t* param, size_t elementSize) {
    if (!fn_) return nullptr;
    FProperty* p = fn_->FindProperty(FName(param, FNAME_Find));
    auto* ap = p ? CastField<FArrayProperty>(p) : nullptr;
    if (!ap) {
        error_ = std::wstring(L"array param not found: ") + param;
        return nullptr;
    }
    if (static_cast<size_t>(ap->GetInner()->GetElementSize()) != elementSize) {
        error_ = std::wstring(L"array element size mismatch: ") + param + L" (" +
                 std::to_wstring(ap->GetInner()->GetElementSize()) + L" vs " + std::to_wstring(elementSize) + L")";
        return nullptr;
    }
    return slot(param, sizeof(FakeTArray));
}

Call& Call::setString(const wchar_t* param, const std::wstring& text) {
    if (uint8_t* p = slot(param, sizeof(FakeTArray))) {
        // FString = TArray<TCHAR>。Num は終端の 0 を含む
        FakeTArray a{text.c_str(), static_cast<int32_t>(text.size() + 1), static_cast<int32_t>(text.size() + 1)};
        std::memcpy(p, &a, sizeof a);
    }
    return *this;
}

bool Call::getRaw(const wchar_t* param, std::vector<uint8_t>& out) {
    if (!fn_) return false;
    FProperty* p = fn_->FindProperty(FName(param, FNAME_Find));
    if (!p) {
        error_ = std::wstring(L"param not found: ") + param;
        return false;
    }
    size_t off = static_cast<size_t>(p->GetOffset_Internal()), size = static_cast<size_t>(p->GetSize());
    if (off + size > buf_.size()) return false;
    out.assign(buf_.begin() + off, buf_.begin() + off + size);
    return true;
}

Call& Call::setRaw(const wchar_t* param, const std::vector<uint8_t>& bytes) {
    if (uint8_t* p = slot(param, bytes.size())) std::memcpy(p, bytes.data(), bytes.size());
    return *this;
}

bool Call::invoke() {
    if (!ok()) return false;
    obj_->ProcessEvent(fn_, buf_.data());
    return true;
}

UObject* objectProperty(UObject* obj, const wchar_t* name) {
    if (!obj) return nullptr;
    FProperty* p = obj->GetPropertyByNameInChain(name);
    if (!p || !CastField<FObjectPropertyBase>(p) || p->GetSize() != sizeof(void*)) return nullptr;
    return *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(obj) + p->GetOffset_Internal());
}

}  // namespace rvrm_mod
