include(GoogleTest)
add_executable(runtime_tests test_radio_core.cpp)
target_link_libraries(runtime_tests PRIVATE atc_runtime GTest::gtest_main Threads::Threads)
gtest_discover_tests(runtime_tests)
