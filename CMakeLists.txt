cmake_minimum_required(VERSION 3.16)

project(EMStudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set RC compiler to use 64-bit windres from mingw810_64
# Temporarily disabled due to windres compilation issues
# if(WIN32)
#     set(CMAKE_RC_COMPILER "C:/Qt/Tools/mingw810_64/bin/windres.exe")
#     set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -F pe-x86-64")
# endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_definitions(-DQT_DISABLE_DEPRECATED_BEFORE=0x060000)

# Explicitly set Qt5_DIR to help CMake find Qt5 configuration files
if(NOT Qt5_DIR)
    set(Qt5_DIR "C:/Qt/5.15.2/mingw81_64/lib/cmake/Qt5")
endif()

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Xml)

# -----------------------------------------------------------------------------
# Versioning (как в .pro)
# -----------------------------------------------------------------------------

set(EMSTUDIO_MAJOR 1)
set(EMSTUDIO_TAG "v${EMSTUDIO_MAJOR}.0")
set(EMSTUDIO_MINOR 0)

find_package(Git QUIET)

if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse -q --verify refs/tags/${EMSTUDIO_TAG}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE HAS_TAG
        OUTPUT_QUIET ERROR_QUIET
    )

    if(HAS_TAG EQUAL 0)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-list --count ${EMSTUDIO_TAG}..HEAD
            OUTPUT_VARIABLE EMSTUDIO_MINOR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    else()
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
            OUTPUT_VARIABLE EMSTUDIO_MINOR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif()

    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --format=%cd --date=format:%Y-%m-%dT%H:%M:%S
        OUTPUT_VARIABLE EMSTUDIO_GIT_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

set(EMSTUDIO_VERSION "${EMSTUDIO_MAJOR}.${EMSTUDIO_MINOR}")

message(STATUS "EMStudio version: ${EMSTUDIO_VERSION}")

# -----------------------------------------------------------------------------
# Include paths
# -----------------------------------------------------------------------------

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/extension
    ${CMAKE_SOURCE_DIR}/QtPropertyBrowser
)

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------

set(SOURCES
    src/main.cpp

    src/headless.cpp
    src/wslHelper.cpp
    src/about.cpp
    src/tips.cpp
    src/keywordseditor.cpp

    QtPropertyBrowser/qtbuttonpropertybrowser.cpp
    QtPropertyBrowser/qteditorfactory.cpp
    QtPropertyBrowser/qtgroupboxpropertybrowser.cpp
    QtPropertyBrowser/qtpropertybrowser.cpp
    QtPropertyBrowser/qtpropertybrowserutils.cpp
    QtPropertyBrowser/qtpropertymanager.cpp
    QtPropertyBrowser/qttreepropertybrowser.cpp
    QtPropertyBrowser/qtvariantproperty.cpp

    src/dielectric.cpp
    extension/fileedit.cpp
    extension/fileeditfactory.cpp
    extension/filepathmanager.cpp
    extension/qlineeditd2.cpp
    extension/variantfactory.cpp
    extension/variantmanager.cpp

    src/finddialog.cpp
    src/gdsreader.cpp
    src/layer.cpp
    src/mainwindow.cpp
    src/material.cpp
    src/preferences.cpp

    src/pythonToEditor.cpp
    src/pythonToStudio.cpp
    src/pythoneditor.cpp
    src/pythonparser.cpp
    src/pythonsyntaxhighlighter.cpp

    src/runOpenEms.cpp
    src/runPalace.cpp

    src/substrate.cpp
    src/substrateview.cpp
    src/verification.cpp
    src/xmlreader.cpp
)

set(HEADERS
    src/wslHelper.h
    src/about.h
    src/keywordseditor.h

    QtPropertyBrowser/qtbuttonpropertybrowser.h
    QtPropertyBrowser/qteditorfactory.h
    QtPropertyBrowser/qtgroupboxpropertybrowser.h
    QtPropertyBrowser/qtpropertybrowser.h
    QtPropertyBrowser/qtpropertybrowserutils_p.h
    QtPropertyBrowser/qtpropertymanager.h
    QtPropertyBrowser/qttreepropertybrowser.h
    QtPropertyBrowser/qtvariantproperty.h
    QtPropertyBrowser/SciDoubleSpinBox.h

    src/dielectric.h
    extension/fileedit.h
    extension/fileeditfactory.h
    extension/filepathmanager.h
    extension/qlineeditd2.h
    extension/variantfactory.h
    extension/variantmanager.h

    src/finddialog.h
    src/layer.h
    src/mainwindow.h
    src/material.h
    src/preferences.h
    src/pythoneditor.h
    src/pythonparser.h
    src/pythonsyntaxhighlighter.h
    src/substrate.h
    src/substrateview.h
)

set(FORMS
    src/about.ui
    src/mainwindow.ui
    src/preferences.ui
)

set(RESOURCES
    icons.qrc
)

if(WIN32)
    # Temporarily disabled due to windres compilation issues
    # set(APP_ICON appicon.rc)
endif()

# -----------------------------------------------------------------------------
# Target
# -----------------------------------------------------------------------------

add_executable(EMStudio
    ${SOURCES}
    ${HEADERS}
    ${FORMS}
    ${RESOURCES}
    ${APP_ICON}
)

target_compile_definitions(EMStudio PRIVATE
    EMSTUDIO_VERSION_STR="${EMSTUDIO_VERSION}"
    EMSTUDIO_MAJOR=${EMSTUDIO_MAJOR}
    EMSTUDIO_GIT_DATE_STR="${EMSTUDIO_GIT_DATE}"
)

target_link_libraries(EMStudio PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Xml
)

# -----------------------------------------------------------------------------
# Post-build copy (как в qmake)
# -----------------------------------------------------------------------------

add_custom_command(TARGET EMStudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Copy scripts/ and keywords/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/scripts"
        "$<TARGET_FILE_DIR:EMStudio>/scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/keywords"
        "$<TARGET_FILE_DIR:EMStudio>/keywords"
    COMMAND ${CMAKE_COMMAND} -E echo "Copy Qt plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_PREFIX_PATH}/plugins"
        "$<TARGET_FILE_DIR:EMStudio>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/qt.conf"
        "$<TARGET_FILE_DIR:EMStudio>/qt.conf"
)

option(EMSTUDIO_BUILD_TESTS "Build EMStudio tests" ON)

if(EMSTUDIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()