# Optional CommonDB ROOM link for EmModel (.emmodel.room) publish.

set(EMSTUDIO_ROOM_SOURCE_DIR "" CACHE PATH
    "Local CommonDB/ROOM checkout (enables EmModel Create on the Output page)")

if(NOT EMSTUDIO_ROOM_SOURCE_DIR)
    if(EXISTS "${CMAKE_SOURCE_DIR}/../CommonDB/CMakeLists.txt")
        set(EMSTUDIO_ROOM_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../CommonDB")
    elseif(DEFINED ENV{USERPROFILE}
           AND EXISTS "$ENV{USERPROFILE}/Documents/CommonDB/CMakeLists.txt")
        set(EMSTUDIO_ROOM_SOURCE_DIR "$ENV{USERPROFILE}/Documents/CommonDB")
    elseif(DEFINED ENV{HOME}
           AND EXISTS "$ENV{HOME}/Documents/CommonDB/CMakeLists.txt")
        set(EMSTUDIO_ROOM_SOURCE_DIR "$ENV{HOME}/Documents/CommonDB")
    endif()
endif()

set(EMSTUDIO_HAS_ROOM OFF)

if(EMSTUDIO_ROOM_SOURCE_DIR AND EXISTS "${EMSTUDIO_ROOM_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "EMStudio: enabling ROOM from ${EMSTUDIO_ROOM_SOURCE_DIR}")

    # Prefer an existing Cap'n Proto prefix (LibMan or CommonDB) to avoid a long bootstrap.
    if(NOT DEFINED CAPNP_ROOT OR CAPNP_ROOT STREQUAL "")
        if(EXISTS "${CMAKE_SOURCE_DIR}/../LibMan/capnp-install/include/capnp/message.h")
            set(CAPNP_ROOT "${CMAKE_SOURCE_DIR}/../LibMan/capnp-install"
                CACHE PATH "Cap'n Proto install prefix" FORCE)
        elseif(EXISTS "${EMSTUDIO_ROOM_SOURCE_DIR}/third_party/capnp-install/include/capnp/message.h")
            set(CAPNP_ROOT "${EMSTUDIO_ROOM_SOURCE_DIR}/third_party/capnp-install"
                CACHE PATH "Cap'n Proto install prefix" FORCE)
        elseif(EXISTS "${EMSTUDIO_ROOM_SOURCE_DIR}/capnp-install/include/capnp/message.h")
            set(CAPNP_ROOT "${EMSTUDIO_ROOM_SOURCE_DIR}/capnp-install"
                CACHE PATH "Cap'n Proto install prefix" FORCE)
        endif()
    endif()

    set(ROOM_BOOTSTRAP_CAPNP ON CACHE BOOL "Bootstrap Cap'n Proto if missing" FORCE)
    set(ROOM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ROOM_BUILD_OAS_TESTS OFF CACHE BOOL "" FORCE)
    set(ROOM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ROOM_BUILD_XSCHEM_TCL OFF CACHE BOOL "" FORCE)

    add_subdirectory("${EMSTUDIO_ROOM_SOURCE_DIR}"
                     "${CMAKE_BINARY_DIR}/_deps/commondb-build" EXCLUDE_FROM_ALL)

    if(TARGET room)
        set(EMSTUDIO_HAS_ROOM ON)
        if(NOT TARGET ROOM::room)
            add_library(ROOM::room ALIAS room)
        endif()
        if(TARGET room_utils AND NOT TARGET ROOM::room_utils)
            add_library(ROOM::room_utils ALIAS room_utils)
        endif()
    else()
        message(WARNING "EMStudio: ROOM subdirectory configured but target 'room' missing")
    endif()
else()
    message(STATUS
        "EMStudio: building without ROOM (EmModel Create disabled). "
        "Set -DEMSTUDIO_ROOM_SOURCE_DIR=.../CommonDB to enable.")
endif()
