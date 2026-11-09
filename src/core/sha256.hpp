// Pure core: minimal SHA-256, vendored from multi-stream so the core stays free of Qt and
// OpenSSL. Used to verify downloaded update packages against the digest GitHub publishes.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace goalbar {

// The 32-byte SHA-256 digest of `data`.
std::array<std::uint8_t, 32> sha256_digest(std::string_view data);

// The digest as 64 lowercase hexadecimal characters, for tests and diagnostics.
std::string sha256_hex(std::string_view data);

} // namespace goalbar
