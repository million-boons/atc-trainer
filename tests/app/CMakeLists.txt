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
