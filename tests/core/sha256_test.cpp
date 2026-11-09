#include "core/sha256.hpp"

#include "test_harness.hpp"

#include <string>

TEST_CASE(sha256_hex_matches_the_standard_vectors)
{
	CHECK_EQ(goalbar::sha256_hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	CHECK_EQ(goalbar::sha256_hex("abc"),
		 std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	CHECK_EQ(goalbar::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
		 std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
	CHECK_EQ(goalbar::sha256_hex(std::string(1000000, 'a')),
		 std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}
