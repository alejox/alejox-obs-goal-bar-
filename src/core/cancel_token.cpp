#include "core/cancel_token.hpp"

#include <utility>

namespace goalbar {

void CancelToken::cancel()
{
	std::lock_guard lock(mutex_);
	if (cancelled_)
		return;
	cancelled_ = true;
	// Runs under the lock, so a concurrent disarm() waits until it is done.
	if (abort_) {
		abort_();
		abort_ran_ = true;
	}
}

bool CancelToken::cancelled() const
{
	std::lock_guard lock(mutex_);
	return cancelled_;
}

AbortHook::AbortHook(CancelToken *token, std::function<void()> abort) : token_(token)
{
	if (!token_) {
		armed_ = true;
		return;
	}
	std::lock_guard lock(token_->mutex_);
	if (token_->cancelled_)
		return;
	token_->abort_ = std::move(abort);
	token_->abort_ran_ = false;
	armed_ = true;
}

AbortHook::~AbortHook()
{
	disarm();
}

bool AbortHook::disarm()
{
	if (!token_ || !armed_)
		return fired_;
	std::lock_guard lock(token_->mutex_);
	fired_ = token_->abort_ran_;
	token_->abort_ = nullptr;
	token_->abort_ran_ = false;
	armed_ = false;
	return fired_;
}

} // namespace goalbar
