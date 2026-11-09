// Pure core: no OBS, no Qt.
#pragma once

#include <functional>
#include <mutex>

namespace goalbar {

// Lets one thread abort a blocking call running on another thread. Once
// cancelled it stays cancelled.
class CancelToken {
public:
	// Marks the token cancelled and runs the armed abort hook, if any.
	void cancel();
	bool cancelled() const;

private:
	friend class AbortHook;

	mutable std::mutex mutex_;
	bool cancelled_ = false;
	std::function<void()> abort_;
	bool abort_ran_ = false;
};

// Arms `abort` on a token for the lifetime of one blocking call. `abort` runs
// at most once, on the cancelling thread, and never after disarm() returns.
// A null token arms nothing.
class AbortHook {
public:
	AbortHook(CancelToken *token, std::function<void()> abort);
	~AbortHook();

	AbortHook(const AbortHook &) = delete;
	AbortHook &operator=(const AbortHook &) = delete;

	// False when the token was already cancelled: do not start the call.
	bool armed() const { return armed_; }

	// Removes the hook. Returns true if `abort` ran. Idempotent.
	bool disarm();

private:
	CancelToken *token_;
	bool armed_ = false;
	bool fired_ = false;
};

} // namespace goalbar
