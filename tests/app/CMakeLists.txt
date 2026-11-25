include(GoogleTest)
# View-state logic of the app, without a window.
find_package(Qt6 6.2 QUIET COMPONENTS Core Qml)
if(Qt6_FOUND)
  set(APP ${CMAKE_SOURCE_DIR}/src/app)
  add_executable(dashboard_tests test_dashboard.cpp ${APP}/DashboardController.h ${APP}/DashboardController.cpp
    ${APP}/StatusFeed.h ${APP}/RowModel.h ${APP}/LabController.h ${APP}/LabController.cpp
    ${APP}/SttController.h ${APP}/SttController.cpp ${APP}/ExerciseController.h ${APP}/ExerciseController.cpp)
  set_target_properties(dashboard_tests PROPERTIES AUTOMOC ON)
  target_include_directories(dashboard_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${APP})
  target_link_libraries(dashboard_tests PRIVATE atc_logic Qt6::Core Qt6::Qml GTest::gtest Threads::Threads)
  gtest_discover_tests(dashboard_tests)
  add_executable(login_tests test_login.cpp ${APP}/AccessController.h ${APP}/AccessController.cpp
    ${APP}/SettingsController.h ${APP}/SettingsController.cpp ${APP}/RecordingsController.h ${APP}/RecordingsController.cpp
    ${APP}/RowModel.h ${APP}/LabController.h ${APP}/LabController.cpp)
  set_target_properties(login_tests PROPERTIES AUTOMOC ON)
  target_include_directories(login_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${APP})
  target_link_libraries(login_tests PRIVATE atc_logic Qt6::Core Qt6::Qml GTest::gtest Threads::Threads)
  gtest_discover_tests(login_tests)
  add_executable(exercise_tests test_exercise.cpp ${APP}/ExerciseController.h ${APP}/ExerciseController.cpp
    ${APP}/ExerciseMock.h ${APP}/ExerciseMock.cpp ${APP}/RowModel.h ${APP}/LabController.h ${APP}/LabController.cpp)
  set_target_properties(exercise_tests PROPERTIES AUTOMOC ON)
  target_include_directories(exercise_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${APP})
  target_link_libraries(exercise_tests PRIVATE atc_logic Qt6::Core Qt6::Qml GTest::gtest Threads::Threads)
  gtest_discover_tests(exercise_tests)
  add_executable(studio_tests test_studio.cpp ${APP}/StudioBackend.h ${APP}/StudioController.h ${APP}/StudioController.cpp
    ${APP}/StudioMock.h ${APP}/StudioMock.cpp ${APP}/AccessController.h ${APP}/AccessController.cpp
    ${APP}/RowModel.h ${APP}/LabController.h ${APP}/LabController.cpp)
  set_target_properties(studio_tests PROPERTIES AUTOMOC ON)
  target_include_directories(studio_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${APP})
  target_link_libraries(studio_tests PRIVATE atc_logic Qt6::Core Qt6::Qml GTest::gtest Threads::Threads)
  gtest_discover_tests(studio_tests)
  add_executable(planner_tests test_planner.cpp ${APP}/PlannerBackend.h ${APP}/PlannerController.h ${APP}/PlannerController.cpp
    ${APP}/PlannerMock.h ${APP}/PlannerMock.cpp ${APP}/AccessController.h ${APP}/AccessController.cpp
    ${APP}/RowModel.h ${APP}/LabController.h ${APP}/LabController.cpp)
  set_target_properties(planner_tests PROPERTIES AUTOMOC ON)
  target_include_directories(planner_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${APP})
  target_link_libraries(planner_tests PRIVATE atc_logic Qt6::Core Qt6::Qml GTest::gtest Threads::Threads)
  gtest_discover_tests(planner_tests)
  add_executable(scenarios_screen_tests test_scenarios.cpp ${APP}/ScenarioBackend.h ${APP}/ScenarioController.h ${APP}/ScenarioController.cpp
    ${APP}/ScenarioMock.h ${APP}/ScenarioMock.cpp ${APP}/RowModel.h ${APP}/LabController.h ${APP}/LabController.cpp)
  set_target_properties(scenarios_screen_tests PROPERTIES AUTOMOC ON)
  target_include_directories(scenarios_screen_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${APP})
  target_link_libraries(scenarios_screen_tests PRIVATE atc_logic Qt6::Core Qt6::Qml GTest::gtest Threads::Threads)
  gtest_discover_tests(scenarios_screen_tests)
  add_executable(history_screen_tests test_history.cpp ${APP}/HistoryBackend.h ${APP}/HistoryController.h ${APP}/HistoryController.cpp
    ${APP}/HistoryMock.h ${APP}/HistoryMock.cpp ${APP}/AccessController.h ${APP}/AccessController.cpp ${APP}/RowModel.h ${APP}/LabController.h ${APP}/LabController.cpp)
  set_target_properties(history_screen_tests PROPERTIES AUTOMOC ON)
  target_include_directories(history_screen_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${APP})
  target_link_libraries(history_screen_tests PRIVATE atc_logic Qt6::Core Qt6::Qml GTest::gtest Threads::Threads)
  gtest_discover_tests(history_screen_tests)
  add_executable(setup_screen_tests test_setup.cpp ${APP}/SetupBackend.h ${APP}/SetupController.h ${APP}/SetupController.cpp
    ${APP}/SetupMock.h ${APP}/SetupMock.cpp ${APP}/AccessController.h ${APP}/AccessController.cpp ${APP}/RowModel.h ${APP}/LabController.h ${APP}/LabController.cpp)
  set_target_properties(setup_screen_tests PROPERTIES AUTOMOC ON)
  target_include_directories(setup_screen_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${APP})
  target_link_libraries(setup_screen_tests PRIVATE atc_logic Qt6::Core Qt6::Qml GTest::gtest Threads::Threads)
  gtest_discover_tests(setup_screen_tests)
  # The live exercise (3b): real engine, simulated time, no radio.
  if(TARGET atc_exercise)
    add_executable(live_exercise_tests test_live_exercise.cpp ${APP}/LiveExercise.h ${APP}/LiveExercise.cpp
      ${APP}/ExerciseView.h ${APP}/ExerciseView.cpp ${APP}/ExerciseController.h ${APP}/ExerciseController.cpp
      ${APP}/StatusFeed.h ${APP}/RowModel.h ${APP}/SttController.h ${APP}/SttController.cpp ${APP}/LabController.h ${APP}/LabController.cpp
      ${APP}/AccessController.h ${APP}/AccessController.cpp ${APP}/SettingsController.h ${APP}/SettingsController.cpp)
    set_target_properties(live_exercise_tests PROPERTIES AUTOMOC ON)
    target_include_directories(live_exercise_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${APP})
    target_link_libraries(live_exercise_tests PRIVATE atc_exercise atc_logic Qt6::Core Qt6::Qml GTest::gtest Threads::Threads)
    gtest_discover_tests(live_exercise_tests)
  endif()
endif()
