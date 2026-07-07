cmake_minimum_required(VERSION 3.16)

project(MdsScope VERSION 5.3.1 LANGUAGES CXX)

execute_process(
    COMMAND git rev-parse --short=9 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE MDS_SCOPE_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE MDS_SCOPE_GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE MDS_SCOPE_GIT_TAG_RESULT
    ERROR_QUIET
)
if(MDS_SCOPE_GIT_TAG_RESULT EQUAL 0 AND MDS_SCOPE_GIT_TAG)
    execute_process(
        COMMAND git rev-list --count "${MDS_SCOPE_GIT_TAG}..HEAD"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE MDS_SCOPE_GIT_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    execute_process(
        COMMAND git rev-list --count HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE MDS_SCOPE_GIT_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
execute_process(
    COMMAND git status --porcelain
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE MDS_SCOPE_GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(MDS_SCOPE_GIT_HASH)
    set(MDS_SCOPE_GIT_VERSION "${PROJECT_VERSION}.r${MDS_SCOPE_GIT_REVISION}.g${MDS_SCOPE_GIT_HASH}")
    if(MDS_SCOPE_GIT_STATUS)
        string(APPEND MDS_SCOPE_GIT_VERSION ".dirty")
    endif()
else()
    set(MDS_SCOPE_GIT_VERSION "unknown")
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND UNIX)
    if("$ENV{USER}" STREQUAL "root" OR DEFINED ENV{SUDO_USER})
        set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Install prefix" FORCE)
    elseif(DEFINED ENV{HOME})
        set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Install prefix" FORCE)
    endif()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Network Concurrent)
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.4 REQUIRED COMPONENTS DBus)
endif()

configure_file(
    cmake_uninstall.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    @ONLY
)

add_custom_target(uninstall
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
)

add_executable(MdsScope
    src/main.cpp
    src/core/core.cpp
    src/core/benchmark.cpp
    src/mds/mds_client.cpp
    src/ui/login_dialog.cpp
    src/ui/plot_widget.cpp
    src/ui/main_window.cpp
    src/mdsscope_app.h
    src/core/mdsscope_internal.h
    src/mds/mds_client.h
    src/ui/point_overlay.h
)

add_executable(transfer
    src/transfer.cpp
    src/core/core.cpp
    src/mdsscope_app.h
    src/core/mdsscope_internal.h
)

set_target_properties(transfer PROPERTIES AUTOMOC OFF)
set_target_properties(MdsScope PROPERTIES WIN32_EXECUTABLE TRUE)

if(APPLE)
    file(GLOB MDS_SCOPE_SOURCE_INDEX_SIGNALS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/source_index/signals/*.txt"
    )
    set(MDS_SCOPE_MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/mdsscope.icns")
    set(MDS_SCOPE_MACOS_ENVIRONMENT_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/environment/init.toml"
        "${CMAKE_CURRENT_SOURCE_DIR}/environment/init.webscp"
    )
    set(MDS_SCOPE_MACOS_RESOURCE_FILES
        "${MDS_SCOPE_MACOS_ICON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/APIurl"
        "${CMAKE_CURRENT_SOURCE_DIR}/source_index/trees.txt"
        ${MDS_SCOPE_MACOS_ENVIRONMENT_FILES}
        ${MDS_SCOPE_SOURCE_INDEX_SIGNALS}
    )
    target_sources(MdsScope PRIVATE ${MDS_SCOPE_MACOS_RESOURCE_FILES})
    set_source_files_properties("${MDS_SCOPE_MACOS_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/APIurl" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/source_index/trees.txt" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/source_index"
    )
    set_source_files_properties(${MDS_SCOPE_MACOS_ENVIRONMENT_FILES} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/environment"
    )
    set_source_files_properties(${MDS_SCOPE_SOURCE_INDEX_SIGNALS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/source_index/signals"
    )
    set_target_properties(MdsScope PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "MdsScope"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.mdsscope.MdsScope"
        MACOSX_BUNDLE_ICON_FILE "mdsscope.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${MDS_SCOPE_GIT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright 2026 Weikang Wang"
    )
endif()

target_include_directories(MdsScope PRIVATE
    src
    src/core
    src/mds
    src/ui
)

target_compile_definitions(MdsScope PRIVATE
    MDSSCOPE_VERSION="${PROJECT_VERSION}"
    MDSSCOPE_GIT_VERSION="${MDS_SCOPE_GIT_VERSION}"
)

target_include_directories(transfer PRIVATE
    src
    src/core
    src/mds
    src/ui
)

target_link_libraries(MdsScope PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
)

target_link_libraries(transfer PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
)

if(UNIX AND NOT APPLE)
    target_link_libraries(MdsScope PRIVATE Qt6::DBus)
    target_link_libraries(transfer PRIVATE Qt6::DBus)
endif()

if(APPLE)
    install(TARGETS MdsScope
        BUNDLE DESTINATION .
        COMPONENT app
    )
    install(PROGRAMS "$<TARGET_FILE:MdsScope>"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME MdsScope
        COMPONENT tools
    )
    install(TARGETS transfer
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT tools
    )

    install(FILES
        environment/init.toml
        environment/init.webscp
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/environment
        COMPONENT tools
    )

    install(FILES
        APIurl
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope
        COMPONENT tools
    )

    install(FILES
        source_index/trees.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
        COMPONENT tools
    )

    install(DIRECTORY
        source_index/signals
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
        COMPONENT tools
        FILES_MATCHING PATTERN "*.txt"
    )
else()
    install(TARGETS MdsScope transfer
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE DESTINATION .
    )

    install(FILES
        environment/init.toml
        environment/init.webscp
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/environment
    )

    install(FILES
        APIurl
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope
    )

    if(UNIX)
        install(FILES
            mdsscope.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        )

        install(FILES
            mdsscope.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        )
    endif()

    install(FILES
        source_index/trees.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
    )

    install(DIRECTORY
        source_index/signals
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
        FILES_MATCHING PATTERN "*.txt"
    )
endif()

set(CPACK_PACKAGE_NAME "mdsscope")
set(CPACK_PACKAGE_VENDOR "MdsScope")
set(CPACK_PACKAGE_CONTACT "MdsScope Maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt desktop application for plotting MDSplus signal data")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_BINARY_DIR}")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "MdsScope Maintainers")
set(CPACK_DEBIAN_PACKAGE_SECTION "science")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

include(CPack)
