#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "/home/staszek/MeshRoute/.pio/libdeps/native/doctest/doctest/doctest.h"
#include <csignal>
TEST_CASE("labelled synthetic fatal after assertion") { CHECK(false); std::raise(SIGSEGV); }
