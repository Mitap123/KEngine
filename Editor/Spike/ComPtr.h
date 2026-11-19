// STATUS: UNVERIFIED (Windows-only helper, never compiled by its author).
// Minimal COM smart pointer (avoids a dependency on <wrl/client.h>).
#pragma once

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    T** put() { reset(); return &p_; }   // for out-parameters; releases any held object first
    explicit operator bool() const { return p_ != nullptr; }
private:
    T* p_ = nullptr;
};
