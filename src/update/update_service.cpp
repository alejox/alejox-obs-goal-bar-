#include "update/update_service.hpp"

#include "net/net.hpp"
#include "update/atomic_file.hpp"
#include "update/install_plan.hpp"

// obs-module.h before plugin-support.h: the other order redefines blogva on MSVC.
#include <obs-module.h>
#include <plugin-support.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QProcess>
#include <QString>
#include <QtGlobal>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <fstream>
#include <system_error>
#include <utility>

namespace goalbar::update {
namespace {

constexpr const char *kManifestName = "staged.json";
constexpr const char *kProbeName = ".obs-goal-bar-update-probe";
constexpr int kCheckTimeoutMs = 15000;
constexpr int kDownloadTimeoutMs = 120000;

namespace fs = std::filesystem;

std::string to_utf8(const fs::path &path)
{
	return path.u8string();
}

bool can_create_file_in(const fs::path &directory)
{
	const fs::path probe = directory / kProbeName;
	{
		std::ofstream file(probe, std::ios::binary | std::ios::trunc);
		if (!file) {
			return false;
		}
	}
	std::error_code ignored;
	fs::remove(probe, ignored);
	return true;
}

// The installer renames the install directory and creates a new one next to it, so both
// the directory and its parent must be writable for this user.
bool can_replace(const fs::path &install_root)
{
	std::error_code error;
	if (!fs::is_directory(install_root, error)) {
		return false;
	}
	return can_create_file_in(install_root) && can_create_file_in(install_root.parent_path());
}

void remove_all_quietly(const fs::path &path)
{
	std::error_code ignored;
	fs::remove_all(path, ignored);
}

// Whether the bytes are the package the manifest describes.
bool matches_manifest(const std::string &content, const StagedUpdate &staged)
{
	if (staged.size != 0 && content.size() != staged.size) {
		return false;
	}
	return staged.sha256.empty() || sha256_matches(content, staged.sha256);
}

bool verify_package(const fs::path &archive, const StagedUpdate &staged)
{
	std::string content;
	return read_whole_file(archive, content) && matches_manifest(content, staged);
}

std::optional<StagedUpdate> read_manifest(const fs::path &staging_dir)
{
	std::string text;
	if (!read_whole_file(staging_dir / kManifestName, text)) {
		return std::nullopt;
	}
	return decode_staged_update(text);
}

// The installer's outcome from the last close. A success is logged and forgotten; a failure
// is kept, so that the same version is not downloaded and attempted again on every start.
std::optional<InstallResult> take_install_result(const fs::path &result_file)
{
	std::string text;
	if (!read_whole_file(result_file, text)) {
		return std::nullopt;
	}
	const auto result = parse_install_result(text);
	if (!result) {
		remove_all_quietly(result_file);
		return std::nullopt;
	}
	if (result->ok) {
		obs_log(LOG_INFO, "update %s was installed when OBS last closed", result->version.c_str());
		remove_all_quietly(result_file);
	} else {
		obs_log(LOG_WARNING, "installing update %s failed when OBS last closed: %s", result->version.c_str(),
			result->detail.c_str());
	}
	return result;
}

std::string user_agent()
{
	return std::string("obs-goal-bar-updater/") + PLUGIN_VERSION;
}

QString to_qstring(const fs::path &path)
{
#if defined(_WIN32)
	return QString::fromStdWString(path.wstring());
#else
	return QString::fromStdString(to_utf8(path));
#endif
}

bool write_binary(const fs::path &path, const std::string &content, bool with_bom)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (with_bom) {
		// Windows PowerShell 5.1 reads a BOM-less script in the system code page.
		file << "\xEF\xBB\xBF";
	}
	file << content;
	file.close();
	return static_cast<bool>(file);
}

bool start_detached(const fs::path &script, UpdatePlatform platform)
{
	QProcess process;
	process.setStandardInputFile(QProcess::nullDevice());
	process.setStandardOutputFile(QProcess::nullDevice());
	process.setStandardErrorFile(QProcess::nullDevice());
	if (platform == UpdatePlatform::WindowsX64) {
		process.setProgram(QStringLiteral("powershell.exe"));
		process.setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
				      QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
				      QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-File"),
				      to_qstring(script)});
#if defined(_WIN32)
		// No console window flashes up while OBS closes.
		process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
			arguments->flags &= ~static_cast<DWORD>(CREATE_NEW_CONSOLE);
			arguments->flags |= CREATE_NO_WINDOW;
		});
#endif
	} else {
		process.setProgram(QStringLiteral("/bin/sh"));
		process.setArguments({to_qstring(script)});
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0) && !defined(_WIN32)
		// Its own session, so the terminal or process group OBS leaves behind cannot stop it.
		process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession |
						 QProcess::UnixProcessFlag::CloseFileDescriptors);
#endif
	}
	return process.startDetached();
}

} // namespace

UpdateService::UpdateService(UpdateServiceConfig config, QObject *parent) : QObject(parent), config_(std::move(config))
{
}

UpdateService::~UpdateService()
{
	if (cancel_) {
		cancel_->cancel();
	}
	if (worker_.joinable()) {
		worker_.join();
	}
}

template<typename Work> void UpdateService::run(UpdateState initial, Work work)
{
	// The previous worker has posted its outcome, so this join is quick.
	if (worker_.joinable()) {
		worker_.join();
	}
	running_ = true;
	cancel_ = std::make_shared<CancelToken>();
	state_ = std::move(initial);
	emit state_changed();

	worker_ = std::thread([this, work = std::move(work), cancel = cancel_]() mutable {
		Outcome outcome = work(*cancel);
		if (cancel->cancelled()) {
			return;
		}
		// The destructor joins this thread, so `this` is alive while posting; Qt drops the
		// queued call if the object is destroyed before it runs.
		QMetaObject::invokeMethod(
			this,
			[this, outcome = std::move(outcome), cancel]() mutable {
				if (!cancel->cancelled()) {
					apply(std::move(outcome));
				}
			},
			Qt::QueuedConnection);
	});
}

void UpdateService::check(bool network)
{
	if (running_) {
		return;
	}
	ready_.reset();
	offer_.reset();
	run(UpdateState{network ? UpdateStatus::Checking : UpdateStatus::Idle, {}, {}},
	    [config = config_, network](CancelToken &cancel) { return check_release(config, cancel, network); });
}

void UpdateService::install()
{
	if (running_ || state_.status != UpdateStatus::Available || !offer_) {
		return;
	}
	const UpdateState offered = state_;
	run(UpdateState{UpdateStatus::Downloading, offered.version, offered.release_url},
	    [config = config_, asset = *offer_, offered](CancelToken &cancel) {
		    return download_and_stage(config, asset, offered, cancel);
	    });
}

void UpdateService::apply(Outcome outcome)
{
	running_ = false;
	state_ = std::move(outcome.state);
	ready_ = std::move(outcome.ready);
	offer_ = std::move(outcome.offer);
	emit state_changed();
}

UpdateService::Outcome UpdateService::check_release(const UpdateServiceConfig &config, CancelToken &cancel,
						    bool network)
{
	Outcome outcome;
	const auto last_install = take_install_result(config.result_file);
	const auto current = parse_semver(config.current_version);
	const bool installable = config.platform && config.install_root && can_replace(*config.install_root);

	// A package staged by an earlier session needs no new download.
	if (const auto staged = read_manifest(config.staging_dir)) {
		const auto version = parse_semver(staged->version);
		const fs::path archive = config.staging_dir / fs::u8path(staged->archive_name);
		if (installable && current && version && compare_semver(*version, *current) > 0 &&
		    verify_package(archive, *staged)) {
			obs_log(LOG_INFO, "update %s is staged and installs when OBS closes", staged->version.c_str());
			outcome.state = {UpdateStatus::Ready, staged->version, {}};
			outcome.ready = ReadyUpdate{*staged, archive};
			return outcome;
		}
	}
	// Installed already, unusable, or from an older check: start over.
	remove_all_quietly(config.staging_dir);
	if (!network) {
		return outcome;
	}

	const net::Response response =
		net::get(std::string(kLatestReleaseUrl),
			 {{"Accept", "application/vnd.github+json"},
			  {"X-GitHub-Api-Version", "2022-11-28"},
			  {"User-Agent", user_agent()}},
			 kCheckTimeoutMs, &cancel);
	if (cancel.cancelled()) {
		return outcome;
	}
	if (response.status == 404) {
		// No published release yet.
		obs_log(LOG_INFO, "update check: no published release yet");
		outcome.state.status = UpdateStatus::UpToDate;
		return outcome;
	}
	if (!response.ok()) {
		obs_log(LOG_WARNING, "update check failed (HTTP %d): %s", response.status, response.error.c_str());
		outcome.state.status = UpdateStatus::CheckFailed;
		return outcome;
	}
	const auto release = parse_latest_release(response.body);
	if (!release) {
		obs_log(LOG_WARNING, "update check failed: unreadable release information");
		outcome.state.status = UpdateStatus::CheckFailed;
		return outcome;
	}

	const UpdatePlatform platform = config.platform.value_or(UpdatePlatform::MacosArm64);
	const UpdateDecision decision = decide_update(config.current_version, *release, platform);
	switch (decision.verdict) {
	case UpdateVerdict::UpToDate:
		obs_log(LOG_INFO, "up to date (latest release %s)", release->tag.c_str());
		outcome.state.status = UpdateStatus::UpToDate;
		return outcome;
	case UpdateVerdict::Unknown:
		obs_log(LOG_WARNING, "update check failed: cannot compare %s with %s", config.current_version.c_str(),
			release->tag.c_str());
		outcome.state.status = UpdateStatus::CheckFailed;
		return outcome;
	case UpdateVerdict::NoPackage:
	case UpdateVerdict::Available:
		break;
	}

	const std::string release_url =
		is_trusted_release_page(release->html_url) ? release->html_url : std::string(kReleasesPageUrl);
	const UpdateState manual{UpdateStatus::Manual, decision.version, release_url};
	if (decision.verdict == UpdateVerdict::NoPackage || !config.platform) {
		obs_log(LOG_INFO, "update %s has no package for this platform", decision.version.c_str());
		outcome.state = manual;
		return outcome;
	}
	if (last_install && !last_install->ok && last_install->version == decision.version) {
		obs_log(LOG_INFO, "update %s failed to install before: offering the manual download",
			decision.version.c_str());
		outcome.state = manual;
		return outcome;
	}
	if (!installable) {
		obs_log(LOG_INFO, "update %s is available, but this install cannot be replaced automatically",
			decision.version.c_str());
		outcome.state = manual;
		return outcome;
	}
	const ReleaseAsset &asset = *decision.asset;
	if (asset.sha256.empty() && asset.size == 0) {
		obs_log(LOG_WARNING, "update %s publishes neither a digest nor a size: not downloading",
			decision.version.c_str());
		outcome.state = manual;
		return outcome;
	}
	if (!is_safe_file_name(asset.name) || !is_trusted_download_url(asset.download_url) ||
	    asset.size > kMaxPackageBytes) {
		obs_log(LOG_WARNING, "update %s has an unexpected package name, location or size",
			decision.version.c_str());
		outcome.state = manual;
		return outcome;
	}

	obs_log(LOG_INFO, "update %s is available (running %s)", decision.version.c_str(),
		config.current_version.c_str());
	outcome.state = {UpdateStatus::Available, decision.version, release_url};
	outcome.offer = asset;
	return outcome;
}

UpdateService::Outcome UpdateService::download_and_stage(const UpdateServiceConfig &config,
							 const ReleaseAsset &asset, const UpdateState &offered,
							 CancelToken &cancel)
{
	Outcome outcome;
	const UpdateState failed{UpdateStatus::DownloadFailed, offered.version, offered.release_url};
	const auto fail = [&](const char *what) {
		obs_log(LOG_WARNING, "update %s download failed: %s", offered.version.c_str(), what);
		remove_all_quietly(config.staging_dir);
		outcome.state = failed;
		return outcome;
	};

	std::error_code error;
	fs::create_directories(config.staging_dir, error);
	if (error) {
		return fail("cannot create the staging folder");
	}

	const net::Response response =
		net::get(asset.download_url, {{"User-Agent", user_agent()}}, kDownloadTimeoutMs, &cancel);
	if (cancel.cancelled()) {
		remove_all_quietly(config.staging_dir);
		return outcome;
	}
	if (!response.ok()) {
		obs_log(LOG_WARNING, "update download: HTTP %d %s", response.status, response.error.c_str());
		return fail("no package received");
	}
	const std::uint64_t limit = asset.size != 0 ? asset.size : kMaxPackageBytes;
	if (response.body.size() > limit) {
		return fail("the download is larger than expected");
	}

	const StagedUpdate staged{offered.version, asset.name, asset.sha256, asset.size};
	if (!matches_manifest(response.body, staged)) {
		return fail("the package does not match the published size or SHA-256");
	}
	const fs::path archive = config.staging_dir / fs::u8path(asset.name);
	const fs::path partial = config.staging_dir / fs::u8path(asset.name + ".part");
	if (!write_binary(partial, response.body, false)) {
		return fail("cannot write the package");
	}
	fs::rename(partial, archive, error);
	if (error || !verify_package(archive, staged)) {
		return fail("cannot store the package");
	}
	if (const auto problem = replace_file_keeping_backup(config.staging_dir / kManifestName,
							     encode_staged_update(staged))) {
		return fail(problem->c_str());
	}

	obs_log(LOG_INFO, "update %s downloaded and verified (%s); installs when OBS closes", offered.version.c_str(),
		staged.sha256.empty() ? "size" : "SHA-256");
	outcome.state = {UpdateStatus::Ready, offered.version, offered.release_url};
	outcome.ready = ReadyUpdate{staged, archive};
	return outcome;
}

bool UpdateService::launch_staged_install()
{
	if (!ready_ || !config_.install_root || !config_.platform) {
		return false;
	}
	InstallPlan plan;
	plan.platform = *config_.platform;
	plan.obs_pid = QCoreApplication::applicationPid();
	plan.version = ready_->staged.version;
	plan.install_root = to_utf8(*config_.install_root);
	plan.archive = to_utf8(ready_->archive);
	plan.staging_dir = to_utf8(config_.staging_dir);
	plan.result_file = to_utf8(config_.result_file);
	const auto script = render_install_script(plan);
	if (!script) {
		obs_log(LOG_WARNING, "update %s not installed: unsafe install paths", plan.version.c_str());
		return false;
	}
	const fs::path script_path = config_.staging_dir / std::string(install_script_name(plan.platform));
	if (!write_binary(script_path, *script, plan.platform == UpdatePlatform::WindowsX64)) {
		obs_log(LOG_WARNING, "update %s not installed: cannot write the installer", plan.version.c_str());
		return false;
	}
	if (!start_detached(script_path, plan.platform)) {
		obs_log(LOG_WARNING, "update %s not installed: cannot start the installer", plan.version.c_str());
		return false;
	}
	obs_log(LOG_INFO, "installing update %s after OBS exits", plan.version.c_str());
	ready_.reset();
	return true;
}

} // namespace goalbar::update
