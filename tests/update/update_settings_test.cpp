#include "update/update_settings_file.hpp"
#include "update/update_settings.hpp"

#include "test_harness.hpp"

#include <filesystem>
#include <fstream>

using namespace goalbar::update;

namespace {

std::filesystem::path fresh_directory(const char *name)
{
	const std::filesystem::path directory = std::filesystem::temp_directory_path() / name;
	std::filesystem::remove_all(directory);
	return directory;
}

UpdateSettings custom_settings()
{
	UpdateSettings settings;
	settings.auto_check = false;
	settings.last_check = 1762650000;
	settings.skipped_version = "0.2.0";
	return settings;
}

} // namespace

TEST_CASE(update_settings_round_trip)
{
	const auto decoded = decode_update_settings(encode_update_settings(custom_settings()));
	REQUIRE(decoded.has_value());
	CHECK(!decoded->auto_check);
	CHECK_EQ(decoded->last_check, static_cast<std::int64_t>(1762650000));
	CHECK_EQ(decoded->skipped_version, std::string("0.2.0"));
}

TEST_CASE(decode_update_settings_defaults_missing_or_odd_fields)
{
	const UpdateSettings empty = decode_update_settings("{}").value_or(custom_settings());
	CHECK(empty.auto_check);
	CHECK_EQ(empty.last_check, static_cast<std::int64_t>(0));
	CHECK(empty.skipped_version.empty());

	const UpdateSettings odd =
		decode_update_settings(R"({"auto_check":"no","last_check":"yesterday","skipped_version":7})")
			.value_or(custom_settings());
	CHECK(odd.auto_check);
	CHECK_EQ(odd.last_check, static_cast<std::int64_t>(0));
	CHECK(odd.skipped_version.empty());

	CHECK(!decode_update_settings("[]").has_value());
	CHECK(!decode_update_settings("{oops").has_value());
}

TEST_CASE(automatic_check_runs_at_most_once_a_day)
{
	UpdateSettings settings;
	const std::int64_t now = 1762650000;
	CHECK(automatic_check_due(settings, now));

	settings.last_check = now - 60;
	CHECK(!automatic_check_due(settings, now));
	settings.last_check = now - kAutomaticCheckIntervalSeconds + 1;
	CHECK(!automatic_check_due(settings, now));
	settings.last_check = now - kAutomaticCheckIntervalSeconds;
	CHECK(automatic_check_due(settings, now));

	// A clock set back must not silence the check for days.
	settings.last_check = now + 3600;
	CHECK(automatic_check_due(settings, now));

	settings.last_check = 0;
	settings.auto_check = false;
	CHECK(!automatic_check_due(settings, now));
}

TEST_CASE(should_offer_respects_the_skipped_version_only_for_automatic_checks)
{
	UpdateSettings settings;
	CHECK(should_offer(settings, "0.2.0", false));
	settings.skipped_version = "0.2.0";
	CHECK(!should_offer(settings, "0.2.0", false));
	CHECK(should_offer(settings, "0.2.0", true));
	CHECK(should_offer(settings, "0.3.0", false));
}

TEST_CASE(update_settings_file_defaults_when_missing)
{
	const UpdateSettings loaded = load_update_settings(fresh_directory("gb_usf_missing") / "update.json");
	CHECK(loaded.auto_check);
	CHECK_EQ(loaded.last_check, static_cast<std::int64_t>(0));
}

TEST_CASE(update_settings_file_saves_into_a_new_directory_and_loads_back)
{
	const auto directory = fresh_directory("gb_usf_round");
	CHECK(!save_update_settings(directory / "update.json", custom_settings()).has_value());
	const UpdateSettings loaded = load_update_settings(directory / "update.json");
	CHECK(!loaded.auto_check);
	CHECK_EQ(loaded.skipped_version, std::string("0.2.0"));
	std::filesystem::remove_all(directory);
}

TEST_CASE(update_settings_file_falls_back_to_the_backup)
{
	const auto directory = fresh_directory("gb_usf_backup");
	CHECK(!save_update_settings(directory / "update.json", custom_settings()).has_value());
	CHECK(!save_update_settings(directory / "update.json", custom_settings()).has_value());
	std::ofstream(directory / "update.json", std::ios::trunc) << "{broken";
	CHECK_EQ(load_update_settings(directory / "update.json").skipped_version, std::string("0.2.0"));
	std::filesystem::remove_all(directory);
}
