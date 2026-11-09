// Qt driver for updates, ported from multi-stream: checks GitHub for a newer release on a
// worker thread, and once the user accepts it downloads this platform's package into the
// staging directory, verifies it, and records it for the installer that runs when OBS
// closes. Every failure is logged and shown as a state; nothing here blocks the UI thread or
// throws.
#pragma once

#include "core/cancel_token.hpp"
#include "update/staging.hpp"
#include "update/update_check.hpp"
#include "update/update_state.hpp"

#include <QObject>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace goalbar::update {

struct UpdateServiceConfig {
	std::string current_version;
	// Holds the package and its manifest; created on demand.
	std::filesystem::path staging_dir;
	// The installer's outcome from the last close; read on the next check.
	std::filesystem::path result_file;
	// The installed plugin directory; nullopt when this install cannot be replaced.
	std::optional<std::filesystem::path> install_root;
	std::optional<UpdatePlatform> platform;
};

// A verified package ready to install.
struct ReadyUpdate {
	StagedUpdate staged;
	std::filesystem::path archive;
};

class UpdateService final : public QObject {
	Q_OBJECT

public:
	explicit UpdateService(UpdateServiceConfig config, QObject *parent = nullptr);
	// Cancels a check or download in flight and waits for its thread (the network layer
	// aborts promptly on cancel).
	~UpdateService() override;

	// Looks for a package staged by an earlier session and, when `network` is true, asks
	// GitHub for the latest release. Ignored while a check or download runs.
	void check(bool network);
	// Downloads, verifies and stages the version the last check found Available.
	void install();

	// Called once as OBS closes: writes the installer for a verified staged package and
	// launches it detached, so it runs after OBS exits. Returns at once; false when nothing
	// was launched.
	bool launch_staged_install();

	bool busy() const { return running_; }
	const UpdateState &state() const { return state_; }

signals:
	void state_changed();

private:
	struct Outcome {
		UpdateState state;
		std::optional<ReadyUpdate> ready;
		std::optional<ReleaseAsset> offer;
	};

	template<typename Work> void run(UpdateState initial, Work work);
	void apply(Outcome outcome);
	static Outcome check_release(const UpdateServiceConfig &config, CancelToken &cancel, bool network);
	static Outcome download_and_stage(const UpdateServiceConfig &config, const ReleaseAsset &asset,
					  const UpdateState &offered, CancelToken &cancel);

	UpdateServiceConfig config_;
	bool running_ = false;
	UpdateState state_;
	std::optional<ReadyUpdate> ready_;
	std::optional<ReleaseAsset> offer_;
	std::shared_ptr<CancelToken> cancel_;
	std::thread worker_;
};

} // namespace goalbar::update
