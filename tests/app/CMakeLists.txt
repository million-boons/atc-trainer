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
  add_executable(exercise_tests test_exercise.cpp ${APP}/ExerciseController.h ${APP}/ExerciseController.cpp
    ${APP}/ExerciseMock.h ${APP}/ExerciseMock.cpp ${APP}/RowModel.h ${APP}/LabController.h ${APP}/LabController.cpp)
  set_target_properties(exercise_tests PROPERTIES AUTOMOC ON)
  target_include_directories(exercise_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${APP})
  target_link_libraries(exercise_tests PRIVATE atc_logic Qt6::Core Qt6::Qml GTest::gtest Threads::Threads)
  gtest_discover_tests(exercise_tests)
endif()
