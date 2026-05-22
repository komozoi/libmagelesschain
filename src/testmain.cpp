
#include <gtest/gtest.h>
#include <csignal>

#include <curl/curl.h>

volatile std::sig_atomic_t gSignalStatus = 0;

int main(int argc, char **argv) {
	curl_global_init(CURL_GLOBAL_DEFAULT);
	::testing::InitGoogleTest(&argc, argv);
	int result = RUN_ALL_TESTS();
	curl_global_cleanup();
	return result;
}
