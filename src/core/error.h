// Error types used for unrecoverable initialization failures. Hot paths use return values and logs.
#pragma once

#include <stdexcept>
#include <string>

namespace lc {

// Generic failure with an actionable message (what failed, where, and what to check).
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The machine lacks a required capability (no hardware ray tracing, no D3D12 12_1 adapter, ...).
// The application exits with code 3 so tests can record "unavailable" instead of "failed".
class UnsupportedHardware : public Error {
public:
    using Error::Error;
};

// Command-line or configuration misuse. The application exits with code 2.
class UsageError : public Error {
public:
    using Error::Error;
};

// Throws lc::Error with "<message> (<file basename>:<line>)".
[[noreturn]] void ThrowError(std::string message, const char* file, int line);

}  // namespace lc

#define LC_THROW(message) ::lc::ThrowError((message), __FILE__, __LINE__)
