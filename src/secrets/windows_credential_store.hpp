// SecretStore backed by the Windows Credential Manager, as generic credentials with
// target = "<target>/<key>". Construction does not touch the store, so it
// cannot prompt the user.
#pragma once

#include "secrets/secret_store.hpp"

#include <string>

namespace goalbar {

class WindowsCredentialStore final : public SecretStore {
public:
	static constexpr const char *kDefaultTarget = "obs-goal-bar";

	explicit WindowsCredentialStore(std::string target = kDefaultTarget) : target_(std::move(target)) {}

	std::optional<SecretError> put(const std::string &key, const std::string &secret) override;
	SecretLookup get(const std::string &key) override;
	std::optional<SecretError> remove(const std::string &key) override;

private:
	std::string target_;
};

} // namespace goalbar
