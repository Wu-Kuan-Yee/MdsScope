# SPDX-FileCopyrightText: 2026 Weikang Wang
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

project(MdsScope VERSION 5.8 LANGUAGES CXX)

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
if(UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(Qt6 6.4 REQUIRED COMPONENTS DBus)
endif()

# Workaround for Xcode 16 / macOS 15 SDK which removed AGL.framework
# Qt's FindWrapOpenGL.cmake unconditionally adds "-framework AGL" if not found.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_gl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_gl_libs)
        set(_new_gl_libs)
        foreach(_lib IN LISTS _gl_libs)
            if(NOT "${_lib}" MATCHES "AGL")
                list(APPEND _new_gl_libs "${_lib}")
            endif()
        endforeach()
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_new_gl_libs}")
    endif()
endif()

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    @ONLY
)

add_custom_target(uninstall
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
)

qt_add_executable(MdsScope
    src/main.cpp
    src/core/api_auth.cpp
    src/core/benchmark.cpp
    src/core/environment_io.cpp
    src/core/mds_helpers.cpp
    src/core/app_paths.cpp
    src/core/visuals.cpp
    src/core/text_utils.cpp
    src/mds/mds_client.cpp
    src/mds/mds_ip_client.cpp
    src/mds/mds_ip_east.cpp
    src/mds/mds_ip_fetch.cpp
    src/mds/mds_ip_pipeline.cpp
    src/mds/mds_ip_protocol.cpp
    src/mds/mds_ip_signal_fetch.cpp
    src/mds/mds_ip_series.cpp
    src/ssh/ssh_tunnel_manager.cpp
    src/ssh/ssh_diagnostic.cpp
    src/ui/base_dialog.hpp
    src/ui/login_dialog.cpp
    src/ui/ssh_dialog.cpp
    src/ui/plot/events.cpp
    src/ui/plot/readout.cpp
    src/ui/plot/render.cpp
    src/ui/plot/state.cpp
    src/ui/plot/view.cpp
    src/ui/main_window/window.cpp
    src/ui/main_window/data.cpp
    src/ui/main_window/environment.cpp
    src/ui/main_window/export.cpp
    src/ui/main_window/layout.cpp
    src/ui/main_window/scale.cpp
    src/ui/main_window/shared.cpp
    src/ui/main_window/shot.cpp
    src/ui/main_window/ssh.cpp
    src/ui/main_window/web_access.cpp
    src/ui/main_window/web_bookmarks.cpp
    src/ui/main_window/ui.cpp
    src/mdsscope_app.hpp
    src/core/mdsscope_internal.hpp
    src/core/text_utils.hpp
    src/mds/mds_client.hpp
    src/mds/mds_ip_client.hpp
    src/ssh/ssh_tunnel_manager.hpp
    src/ssh/ssh_diagnostic.hpp
    src/ssh/ssh_settings.hpp
    src/ui/ssh_dialog.hpp
    src/ui/main_window/about_dialog.hpp
    src/ui/main_window/export_dialog.hpp
    src/ui/main_window/layout_dialog.hpp
    src/ui/main_window/signal_dialogs.hpp
    src/ui/main_window/shared.hpp
    src/ui/main_window/theme.hpp
    src/ui/plot/helpers.hpp
)

if(NOT ANDROID AND NOT IOS)
add_executable(transfer
    src/transfer.cpp
    src/core/api_auth.cpp
    src/core/environment_io.cpp
    src/core/mds_helpers.cpp
    src/core/app_paths.cpp
    src/core/visuals.cpp
    src/core/text_utils.cpp
    src/mdsscope_app.hpp
    src/core/mdsscope_internal.hpp
    src/core/text_utils.hpp
)

set_target_properties(transfer PROPERTIES AUTOMOC OFF)
endif()
set_target_properties(MdsScope PROPERTIES WIN32_EXECUTABLE TRUE)

    file(GLOB MDS_SCOPE_SOURCE_INDEX_SIGNALS RELATIVE "${PROJECT_SOURCE_DIR}" CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/resources/source_index/signals/*.txt"
    )
    qt_add_resources(MdsScope "app_resources"
        PREFIX "/"
        FILES
            resources/APIurl
            resources/environment/init.toml
            resources/environment/init.webscp
            resources/source_index/trees.txt
            images/down_arrow.png
            ${MDS_SCOPE_SOURCE_INDEX_SIGNALS}
    )

if(APPLE)
    set(MDS_SCOPE_MACOS_ICON "${PROJECT_SOURCE_DIR}/packaging/macos/mdsscope.icns")
    target_sources(MdsScope PRIVATE "${MDS_SCOPE_MACOS_ICON}")
    set_source_files_properties("${MDS_SCOPE_MACOS_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    if(NOT IOS)
    set_target_properties(MdsScope PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist"
    )
    else()
    set_target_properties(MdsScope PROPERTIES
        XCODE_ATTRIBUTE_INFOPLIST_KEY_NSAppTransportSecurity "<dict><key>NSAllowsArbitraryLoads</key><true/></dict>"
    )
    endif()
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
    src/ssh
)


# ─── Android deployment ────────────────────────────────────────────────────
if(ANDROID)
    file(COPY "${PROJECT_SOURCE_DIR}/resources" DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/packaging/android/assets")
    set_target_properties(MdsScope PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/packaging/android"
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 34
    )
    target_sources(MdsScope PRIVATE
        packaging/android/AndroidManifest.xml
        packaging/android/res/values/strings.xml
        packaging/android/res/values/styles.xml
        packaging/android/res/xml/network_security_config.xml
    )
    set_target_properties(MdsScope PROPERTIES QT_ANDROID_EXTRA_LIBS "")
endif()

# ─── iOS deployment ────────────────────────────────────────────────────────
if(IOS)
    set_target_properties(MdsScope PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.mdsscope.MdsScope"
        MACOSX_BUNDLE_BUNDLE_NAME "MdsScope"
        MACOSX_BUNDLE_BUNDLE_VERSION "${MDS_SCOPE_GIT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/ios/Info.plist.in"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.mdsscope.MdsScope"
        XCODE_ATTRIBUTE_INFOPLIST_KEY_UIFileSharingEnabled "YES"
        XCODE_ATTRIBUTE_INFOPLIST_KEY_LSSupportsOpeningDocumentsInPlace "YES"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    )
    target_sources(MdsScope PRIVATE src/core/ios_network_trigger.mm)
endif()

target_compile_definitions(MdsScope PRIVATE
    MDSSCOPE_VERSION="${PROJECT_VERSION}"
    MDSSCOPE_GIT_VERSION="${MDS_SCOPE_GIT_VERSION}"
)

if(NOT ANDROID AND NOT IOS)
target_include_directories(transfer PRIVATE
    src
    src/core
    src/mds
    src/ui
    src/ssh
)
endif()

target_link_libraries(MdsScope PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
)

if(ANDROID)
    if(EXISTS "${PROJECT_SOURCE_DIR}/packaging/android_openssl/android_openssl.cmake")
        message(STATUS "Integrating android_openssl for Android TLS support.")
        include("${PROJECT_SOURCE_DIR}/packaging/android_openssl/android_openssl.cmake")
        add_android_openssl_libraries(MdsScope)
    else()
        message(WARNING "android_openssl not found in packaging/android_openssl. TLS/HTTPS may fail on Android. "
                        "Clone https://github.com/KDAB/android_openssl into packaging/android_openssl to enable it.")
    endif()
endif()

# libssh2 — native SSH client.
# Crypto backend per platform:
#   - macOS:       system OpenSSL (Homebrew)
#   - Linux:       system OpenSSL
#   - Windows:     WinCNG (built into Windows, no external dependency)
#   - Android:     OpenSSL via android_openssl submodule
#   - iOS/iPadOS:  requires a prebuilt OpenSSL static library.
#                  Place libssl.a and libcrypto.a in packaging/ios_openssl/
#                  and set CRYPTO_BACKEND=OpenSSL below.
if(EXISTS "${PROJECT_SOURCE_DIR}/packaging/libssh2/CMakeLists.txt")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

    set(MDSSCOPE_SKIP_LIBSSH2 FALSE)

    if(IOS)
        # iOS: check for OpenSSL in packaging/ios_openssl/ (run build_ios_openssl.sh).
        set(IOS_OPENSSL_DIR "${PROJECT_SOURCE_DIR}/packaging/ios_openssl")
        # Accept both flat (libssl.a at root) and make-install layout (lib/libssl.a).
        if(EXISTS "${IOS_OPENSSL_DIR}/libssl.a" AND EXISTS "${IOS_OPENSSL_DIR}/libcrypto.a")
            set(IOS_SSL "${IOS_OPENSSL_DIR}/libssl.a")
            set(IOS_CRYPTO "${IOS_OPENSSL_DIR}/libcrypto.a")
        elseif(EXISTS "${IOS_OPENSSL_DIR}/lib/libssl.a" AND EXISTS "${IOS_OPENSSL_DIR}/lib/libcrypto.a")
            set(IOS_SSL "${IOS_OPENSSL_DIR}/lib/libssl.a")
            set(IOS_CRYPTO "${IOS_OPENSSL_DIR}/lib/libcrypto.a")
        endif()
        if(DEFINED IOS_SSL)
            set(CRYPTO_BACKEND "OpenSSL" CACHE STRING "" FORCE)
            set(OPENSSL_INCLUDE_DIR "${IOS_OPENSSL_DIR}/include" CACHE PATH "" FORCE)
            set(OPENSSL_CRYPTO_LIBRARY "${IOS_CRYPTO}" CACHE FILEPATH "" FORCE)
            set(OPENSSL_SSL_LIBRARY "${IOS_SSL}" CACHE FILEPATH "" FORCE)
            message(STATUS "Using OpenSSL for iOS: ${IOS_SSL}")
        else()
            message(WARNING "iOS OpenSSL not found at ${IOS_OPENSSL_DIR}/."
                            " SSH tunneling disabled on iOS."
                            " Run packaging/build_ios_openssl.sh to build it.")
            set(MDSSCOPE_SKIP_LIBSSH2 TRUE)
        endif()
    elseif(WIN32)
        set(CRYPTO_BACKEND "WinCNG" CACHE STRING "" FORCE)
    elseif(ANDROID)
        # Point libssh2 at the android_openssl submodule.
        set(CRYPTO_BACKEND "OpenSSL" CACHE STRING "" FORCE)
        if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5.0)
            set(ANDROID_SSL_VER "ssl_3")
        else()
            set(ANDROID_SSL_VER "ssl_1.1")
        endif()
        set(ANDROID_SSL_DIR "${PROJECT_SOURCE_DIR}/packaging/android_openssl/${ANDROID_SSL_VER}")
        set(ANDROID_SSL_LIB "${ANDROID_SSL_DIR}/${CMAKE_ANDROID_ARCH_ABI}")
        set(OPENSSL_ROOT_DIR "${ANDROID_SSL_DIR}" CACHE PATH "" FORCE)
        set(OPENSSL_INCLUDE_DIR "${ANDROID_SSL_DIR}/include" CACHE PATH "" FORCE)
        if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5.0)
            set(OPENSSL_CRYPTO_LIBRARY "${ANDROID_SSL_LIB}/libcrypto_3.so" CACHE FILEPATH "" FORCE)
            set(OPENSSL_SSL_LIBRARY "${ANDROID_SSL_LIB}/libssl_3.so" CACHE FILEPATH "" FORCE)
        else()
            set(OPENSSL_CRYPTO_LIBRARY "${ANDROID_SSL_LIB}/libcrypto_1_1.so" CACHE FILEPATH "" FORCE)
            set(OPENSSL_SSL_LIBRARY "${ANDROID_SSL_LIB}/libssl_1_1.so" CACHE FILEPATH "" FORCE)
        endif()
    else()
        # macOS / Linux: use system OpenSSL.
        set(CRYPTO_BACKEND "OpenSSL" CACHE STRING "" FORCE)
    endif()

    if(NOT MDSSCOPE_SKIP_LIBSSH2)
        add_subdirectory(${PROJECT_SOURCE_DIR}/packaging/libssh2
                         ${CMAKE_CURRENT_BINARY_DIR}/libssh2
                         EXCLUDE_FROM_ALL)
        target_link_libraries(MdsScope PRIVATE libssh2_static)
        target_include_directories(MdsScope PRIVATE
            ${PROJECT_SOURCE_DIR}/packaging/libssh2/include)
        target_compile_definitions(MdsScope PRIVATE MDSSCOPE_HAS_LIBSSH2=1)
    endif()
endif()

if(NOT ANDROID AND NOT IOS)
target_link_libraries(transfer PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(MdsScope PRIVATE Qt6::DBus)
    if(NOT ANDROID AND NOT IOS)
        target_link_libraries(transfer PRIVATE Qt6::DBus)
    endif()
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
    if(TARGET transfer)
    install(TARGETS transfer
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT tools
    )
    endif()

    install(FILES
        "${PROJECT_SOURCE_DIR}/resources/environment/init.toml"
        "${PROJECT_SOURCE_DIR}/resources/environment/init.webscp"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/environment
        COMPONENT tools
    )

    install(FILES
        "${PROJECT_SOURCE_DIR}/resources/APIurl"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope
        COMPONENT tools
    )

    install(FILES
        "${PROJECT_SOURCE_DIR}/resources/source_index/trees.txt"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
        COMPONENT tools
    )

    install(DIRECTORY
        "${PROJECT_SOURCE_DIR}/resources/source_index/signals"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
        COMPONENT tools
        FILES_MATCHING PATTERN "*.txt"
    )
else()
    install(TARGETS MdsScope
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION lib
        BUNDLE DESTINATION .
    )
    if(TARGET transfer)
        install(TARGETS transfer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()

    install(FILES
        "${PROJECT_SOURCE_DIR}/resources/environment/init.toml"
        "${PROJECT_SOURCE_DIR}/resources/environment/init.webscp"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/environment
    )

    install(FILES
        "${PROJECT_SOURCE_DIR}/resources/APIurl"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope
    )

    if(UNIX)
        install(FILES
            "${PROJECT_SOURCE_DIR}/packaging/linux/mdsscope.svg"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        )

        install(FILES
            "${PROJECT_SOURCE_DIR}/packaging/linux/mdsscope.desktop"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        )
    endif()

    install(FILES
        "${PROJECT_SOURCE_DIR}/resources/source_index/trees.txt"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
    )

    install(DIRECTORY
        "${PROJECT_SOURCE_DIR}/resources/source_index/signals"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
        FILES_MATCHING PATTERN "*.txt"
    )
endif()

if(NOT ANDROID AND NOT IOS)
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
endif()
