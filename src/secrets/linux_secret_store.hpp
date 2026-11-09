// SecretStore backed by the freedesktop Secret Service through libsecret, as items in
// the default collection with attributes service = the plugin's service name and
// key = the key. Construction does not touch the service, so it
// cannot prompt the user. When no service is reachable every operation reports
// SecretError::Kind::Unavailable; secrets are never written anywhere else.
#pragma once

#include "secrets/secret_store.hpp"

#include <string>

namespace goalbar {

class LinuxSecretStore final : public SecretStore {
public:
	static constexpr const char *kDefaultService = "com.alejox.obs-goal-bar";

	explicit LinuxSecretStore(std::string service = kDefaultService) : service_(std::move(service)) {}

	const std::string &service() const { return service_; }

	std::optional<SecretError> put(const std::string &key, const std::string &secret) override;
	SecretLookup get(const std::string &key) override;
	std::optional<SecretError> remove(const std::string &key) override;

private:
	std::string service_;
};

} // namespace goalbar
