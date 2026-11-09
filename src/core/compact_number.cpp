#include "core/compact_number.hpp"

namespace goalbar {
namespace {

struct Unit {
	uint64_t size;
	char suffix;
};

// Largest first; the last unit absorbs everything above it.
constexpr Unit kUnits[] = {
	{1000000000000ULL, 'T'},
	{1000000000ULL, 'B'},
	{1000000ULL, 'M'},
	{1000ULL, 'K'},
};

} // namespace

std::string format_compact(uint64_t value)
{
	for (const Unit &unit : kUnits) {
		if (value < unit.size)
			continue;

		const uint64_t whole = value / unit.size;
		const int decimals = whole >= 100 ? 0 : whole >= 10 ? 1 : 2;
		uint64_t divisor = unit.size;
		for (int i = 0; i < decimals; ++i)
			divisor /= 10;
		const uint64_t fraction = (value % unit.size) / divisor;

		std::string text = std::to_string(whole);
		if (decimals > 0 && fraction > 0) {
			std::string digits = std::to_string(fraction);
			digits.insert(0, static_cast<size_t>(decimals) - digits.size(), '0');
			while (digits.back() == '0')
				digits.pop_back();
			text += '.' + digits;
		}
		return text + unit.suffix;
	}
	return std::to_string(value);
}

} // namespace goalbar
