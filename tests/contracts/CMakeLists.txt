include(GoogleTest)
add_executable(contract_tests test_contracts.cpp)
target_link_libraries(contract_tests PRIVATE atc_parser atc_engine atc_voice atc_runtime GTest::gtest_main Threads::Threads)
gtest_discover_tests(contract_tests)
