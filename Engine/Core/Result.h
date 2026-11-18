// K Engine — Engine/Core/Result.h
// Minimal explicit-error type (C++20 has no std::expected).
//
// Error::code is a stable, machine-readable identifier (for example
// "scene.object.duplicate_uuid"); Error::message is human-readable and may
// change. Tests and tools should match on `code`, never on `message`.
#pragma once

#include <string>
#include <utility>
#include <variant>

namespace k {

struct Error {
    std::string code;
    std::string message;
};

template <class T>
class [[nodiscard]] Result {
public:
    Result(T value) : v_(std::move(value)) {}             // NOLINT(google-explicit-constructor)
    Result(Error error) : v_(std::move(error)) {}         // NOLINT(google-explicit-constructor)

    bool ok() const { return v_.index() == 0; }
    explicit operator bool() const { return ok(); }

    // Precondition: ok(). Violating it throws std::bad_variant_access.
    T& value() & { return std::get<0>(v_); }
    const T& value() const& { return std::get<0>(v_); }
    T&& value() && { return std::get<0>(std::move(v_)); }

    // Precondition: !ok().
    const Error& error() const { return std::get<1>(v_); }

private:
    std::variant<T, Error> v_;
};

// Result without a payload.
class [[nodiscard]] Status {
public:
    Status() = default;
    Status(Error error) : error_(std::move(error)), ok_(false) {}  // NOLINT(google-explicit-constructor)

    static Status success() { return Status(); }

    bool ok() const { return ok_; }
    explicit operator bool() const { return ok_; }
    const Error& error() const { return error_; }

private:
    Error error_;
    bool ok_ = true;
};

inline Error make_error(std::string code, std::string message) {
    return Error{std::move(code), std::move(message)};
}

}  // namespace k
