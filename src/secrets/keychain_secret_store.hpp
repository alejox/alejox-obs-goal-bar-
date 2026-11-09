// SecretStore backed by the macOS login Keychain, as generic password items with
// service = the plugin's service name and account = the key.
#pragma once

#include "secrets/secret_store.hpp"

namespace goalbar {

class KeychainSecretStore final : public SecretStore {
public:
	static constexpr const char *kDefaultService = "com.alejox.obs-goal-bar";

	explicit KeychainSecretStore(std::string service = kDefaultService) : service_(std::move(service)) {}

	const std::string &service() const { return service_; }

	std::optional<SecretError> put(const std::string &key, const std::string &secret) override;
	SecretLookup get(const std::string &key) override;
	std::optional<SecretError> remove(const std::string &key) override;

private:
	std::string service_;
};

} // namespace goalbar
