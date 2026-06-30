cmake_minimum_required(VERSION 3.16)

project(MdsScope VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Network Concurrent DBus)

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

target_include_directories(MdsScope PRIVATE
    src
    src/core
    src/mds
    src/ui
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
    Qt6::DBus
)

target_link_libraries(transfer PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::DBus
)

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

install(FILES
    source_index/trees.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
)

install(DIRECTORY
    source_index/signals
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
    FILES_MATCHING PATTERN "*.txt"
)
