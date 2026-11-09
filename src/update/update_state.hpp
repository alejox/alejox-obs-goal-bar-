// What the updater is doing, as the update controller shows it.
#pragma once

#include <string>

namespace goalbar::update {

enum class UpdateStatus {
	// Nothing known yet, or the check was skipped (not due).
	Idle,
	Checking,
	// The latest release is not newer than the running version.
	UpToDate,
	// A newer version can be installed: waiting for the user to accept it.
	Available,
	Downloading,
	// Verified and staged; installs after OBS closes.
	Ready,
	// A newer version exists but cannot be installed automatically; offer the release page.
	Manual,
	// The check failed; nothing changed.
	CheckFailed,
	// The download or its verification failed; nothing was staged.
	DownloadFailed,
};

struct UpdateState {
	UpdateStatus status = UpdateStatus::Idle;
	// The newer version, for Available, Downloading, Ready and Manual.
	std::string version;
	// The release page, for Available and Manual (already checked to be this project's).
	std::string release_url;
};

} // namespace goalbar::update
