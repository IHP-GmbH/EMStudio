project(EMStudioTests LANGUAGES CXX)

find_package(Qt5 REQUIRED COMPONENTS Test Core Gui Widgets Xml)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_definitions(-DEMSTUDIO_TESTING)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/extension
    ${CMAKE_SOURCE_DIR}/QtPropertyBrowser
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(TEST_SOURCES
    main.cpp
    test_utils.cpp

    tst_about_dialog.cpp
    tst_find_dialog.cpp
    tst_headless_dispatch.cpp
    tst_keywords_editor_dialog.cpp
    tst_mainwindow_ports.cpp
    tst_openems_golden.cpp
    tst_palace_golden.cpp
    tst_preferences_dialog.cpp
    tst_python_editor.cpp
    tst_wsl_helper.cpp

    ${CMAKE_SOURCE_DIR}/icons.qrc
)

set(APP_SOURCES_FOR_TESTS ${SOURCES})

list(REMOVE_ITEM APP_SOURCES_FOR_TESTS src/main.cpp)

list(TRANSFORM APP_SOURCES_FOR_TESTS PREPEND "${CMAKE_SOURCE_DIR}/")
list(TRANSFORM HEADERS PREPEND "${CMAKE_SOURCE_DIR}/")
list(TRANSFORM FORMS PREPEND "${CMAKE_SOURCE_DIR}/")

add_executable(emstudio_golden_tests
    ${TEST_SOURCES}
    ${APP_SOURCES_FOR_TESTS}
    ${HEADERS}
    ${FORMS}
)

target_compile_definitions(emstudio_golden_tests PRIVATE
    EMSTUDIO_VERSION_STR="${EMSTUDIO_VERSION}"
    EMSTUDIO_MAJOR=${EMSTUDIO_MAJOR}
    EMSTUDIO_GIT_DATE_STR="${EMSTUDIO_GIT_DATE}"
)

target_link_libraries(emstudio_golden_tests PRIVATE
    Qt5::Test
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Xml
)

add_test(NAME EMStudioGoldenTests COMMAND emstudio_golden_tests)