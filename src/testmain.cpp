
#include <gtest/gtest.h>
#include <csignal>

volatile std::sig_atomic_t gSignalStatus = 0;

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	int result = RUN_ALL_TESTS();
	return result;
}
