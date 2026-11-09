// SecretStore for tests. Holds secrets in memory only.
#pragma once

#include "secrets/secret_store.hpp"

#include <map>

namespace goalbar {

class InMemorySecretStore final : public SecretStore {
public:
	std::optional<SecretError> put(const std::string &key, const std::string &secret) override
	{
		secrets_[key] = secret;
		return std::nullopt;
	}

	SecretLookup get(const std::string &key) override
	{
		const auto found = secrets_.find(key);
		if (found == secrets_.end()) {
			return {};
		}
		return {found->second, std::nullopt};
	}

	std::optional<SecretError> remove(const std::string &key) override
	{
		secrets_.erase(key);
		return std::nullopt;
	}

private:
	std::map<std::string, std::string> secrets_;
};

} // namespace goalbar
