include(GoogleTest)
add_executable(runtime_tests test_radio_core.cpp)
target_link_libraries(runtime_tests PRIVATE atc_runtime GTest::gtest_main Threads::Threads)
gtest_discover_tests(runtime_tests)
# Against a private PulseAudio server with a null output; skipped where pulseaudio is not installed.
if(TARGET atc_pulse)
  add_executable(player_tests test_pulse_player.cpp)
  target_link_libraries(player_tests PRIVATE atc_pulse GTest::gtest_main Threads::Threads)
  gtest_discover_tests(player_tests)
endif()
# The status logic of the app, without a window.
find_package(Qt6 6.2 QUIET COMPONENTS Core Qml)
if(Qt6_FOUND)
  add_executable(status_tests test_status.cpp ${CMAKE_SOURCE_DIR}/src/app/StatusController.h ${CMAKE_SOURCE_DIR}/src/app/StatusController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/StatusFeed.h ${CMAKE_SOURCE_DIR}/src/app/RowModel.h
    ${CMAKE_SOURCE_DIR}/src/app/LabController.h ${CMAKE_SOURCE_DIR}/src/app/LabController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/DashboardController.h ${CMAKE_SOURCE_DIR}/src/app/DashboardController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/SttController.h ${CMAKE_SOURCE_DIR}/src/app/SttController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ExerciseController.h ${CMAKE_SOURCE_DIR}/src/app/ExerciseController.cpp)
  set_target_properties(status_tests PROPERTIES AUTOMOC ON)
  target_include_directories(status_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/app)
  target_link_libraries(status_tests PRIVATE atc_runtime Qt6::Core Qt6::Qml GTest::gtest Threads::Threads)
  gtest_discover_tests(status_tests)
  # The live backend, without a window, against a private PulseAudio server.
  if(TARGET atc_devices)
    set(A ${CMAKE_SOURCE_DIR}/src/app)
    add_executable(live_tests test_live.cpp ${A}/LiveBackend.h ${A}/LiveBackend.cpp ${A}/LabChecks.h ${A}/LabChecks.cpp
      ${A}/StatusController.h ${A}/StatusController.cpp ${A}/StatusFeed.h ${A}/RowModel.h ${A}/LabController.h ${A}/LabController.cpp
      ${A}/SttController.h ${A}/SttController.cpp)
    set_target_properties(live_tests PROPERTIES AUTOMOC ON)
    target_include_directories(live_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${A})
    target_link_libraries(live_tests PRIVATE atc_devices atc_logic Qt6::Core Qt6::Qml GTest::gtest Threads::Threads)
    gtest_discover_tests(live_tests)
  endif()
endif()
