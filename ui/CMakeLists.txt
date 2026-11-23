# Qt UI. Qt 6.2 APIs only (D-16): no qt_standard_project_setup, no loadFromModule.
set(CMAKE_AUTOMOC ON)
set(APP ${CMAKE_SOURCE_DIR}/src/app)

qt_add_executable(atc_trainer ${APP}/main.cpp)
target_include_directories(atc_trainer PRIVATE ${CMAKE_SOURCE_DIR}/src ${APP})

set_source_files_properties(theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(atc_trainer
  URI Atc
  VERSION 1.0
  RESOURCE_PREFIX /
  OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Atc
  QML_FILES
    Main.qml
    theme/Theme.qml
    components/Label.qml components/StateIcon.qml components/Card.qml components/BaseButton.qml
    components/PrimaryButton.qml components/SecondaryButton.qml components/StatusChip.qml
    components/Tag.qml components/StatusBar.qml components/NavRail.qml components/AlertBanner.qml
    components/TxRxIndicator.qml components/LevelMeter.qml components/ConfidenceBar.qml
    components/Waveform.qml components/CheckCell.qml components/PttButtonTile.qml
    components/ModelSelector.qml
    screens/SpeechLab.qml
    mock/MockPanel.qml
  SOURCES
    ${APP}/RowModel.h ${APP}/LabController.h ${APP}/LabController.cpp
    ${APP}/SttController.h ${APP}/SttController.cpp
    ${APP}/Painted.h ${APP}/Painted.cpp ${APP}/MockBackend.h ${APP}/MockBackend.cpp)

qt_add_resources(atc_trainer fonts PREFIX /Atc FILES
  fonts/Inter-Regular.otf fonts/Inter-SemiBold.otf fonts/Inter-Bold.otf
  fonts/JetBrainsMono-Regular.ttf fonts/JetBrainsMono-Bold.ttf)

target_link_libraries(atc_trainer PRIVATE atc_logic Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
if(TARGET atc_stt)
  target_link_libraries(atc_trainer PRIVATE atc_stt)
  target_compile_definitions(atc_trainer PRIVATE ATC_HAVE_STT)
endif()
if(TARGET atc_pulse)
  target_link_libraries(atc_trainer PRIVATE atc_pulse)
  target_compile_definitions(atc_trainer PRIVATE ATC_HAVE_PULSE)
endif()
