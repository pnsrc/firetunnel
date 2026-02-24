cmake_minimum_required(VERSION 3.16)
project(trusttunnel-qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Widgets Network REQUIRED)
set(TRUSTTUNNEL_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "Path to TrustTunnelClient repository root")

set(FIRETUNNEL_GIT_VERSION "unknown")
find_package(Git QUIET)
if (GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE FIRETUNNEL_GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

add_executable(trusttunnel-qt MACOSX_BUNDLE
    main.cpp
    src/ui/MainWindow.cpp
    include/ui/MainWindow.h
    src/core/AppSettings.cpp
    include/core/AppSettings.h
    src/core/ConfigStore.cpp
    include/core/ConfigStore.h
    src/core/ConfigInspector.cpp
    include/core/ConfigInspector.h
    src/core/AppUiUtils.cpp
    include/core/AppUiUtils.h
    src/ui/SettingsDialog.cpp
    include/ui/SettingsDialog.h
    src/vpn/qt_trusttunnel_client.cpp
    include/vpn/qt_trusttunnel_client.h
)

target_compile_definitions(trusttunnel-qt PRIVATE
    FIRETUNNEL_VERSION="${PROJECT_VERSION}"
    FIRETUNNEL_GIT_VERSION="${FIRETUNNEL_GIT_VERSION}"
)

add_executable(trusttunnel-qt-helper
    src/vpn/helper_main.cpp
)

target_include_directories(trusttunnel-qt PRIVATE
    include
    include/core
    include/ui
    include/vpn
)
target_include_directories(trusttunnel-qt-helper PRIVATE
    include
    include/core
    include/ui
    include/vpn
)

if (TARGET vpnlibs_trusttunnel)
    target_link_libraries(trusttunnel-qt PRIVATE Qt6::Widgets Qt6::Network vpnlibs_trusttunnel)
    target_link_libraries(trusttunnel-qt-helper PRIVATE vpnlibs_trusttunnel)
    target_include_directories(trusttunnel-qt PRIVATE
        ${TRUSTTUNNEL_REPO_ROOT}/trusttunnel/include
        ${TRUSTTUNNEL_REPO_ROOT}/common/include
        ${TRUSTTUNNEL_REPO_ROOT}/core/include
        ${TRUSTTUNNEL_REPO_ROOT}/net/include
        ${TRUSTTUNNEL_REPO_ROOT}/tcpip/include
    )
    target_include_directories(trusttunnel-qt-helper PRIVATE
        ${TRUSTTUNNEL_REPO_ROOT}/trusttunnel/include
        ${TRUSTTUNNEL_REPO_ROOT}/common/include
        ${TRUSTTUNNEL_REPO_ROOT}/core/include
        ${TRUSTTUNNEL_REPO_ROOT}/net/include
        ${TRUSTTUNNEL_REPO_ROOT}/tcpip/include
    )
else()
    if (NOT EXISTS "${TRUSTTUNNEL_REPO_ROOT}/trusttunnel-core/CMakeLists.txt")
        message(FATAL_ERROR
            "trusttunnel-core not found.\n"
            "Set TRUSTTUNNEL_REPO_ROOT to the TrustTunnelClient root containing trusttunnel-core,\n"
            "or build this client from inside the monorepo where vpnlibs_trusttunnel target is available.")
    endif()
    add_subdirectory(${TRUSTTUNNEL_REPO_ROOT}/trusttunnel-core trusttunnel-core-build)
    target_link_libraries(trusttunnel-qt PRIVATE Qt6::Widgets Qt6::Network trusttunnel_core)
    target_link_libraries(trusttunnel-qt-helper PRIVATE trusttunnel_core)
    target_include_directories(trusttunnel-qt PRIVATE
        ${TRUSTTUNNEL_REPO_ROOT}/trusttunnel/include
        ${TRUSTTUNNEL_REPO_ROOT}/trusttunnel/src
        ${TRUSTTUNNEL_REPO_ROOT}/common/include
        ${TRUSTTUNNEL_REPO_ROOT}/core/include
        ${TRUSTTUNNEL_REPO_ROOT}/net/include
        ${TRUSTTUNNEL_REPO_ROOT}/tcpip/include
    )
    target_include_directories(trusttunnel-qt-helper PRIVATE
        ${TRUSTTUNNEL_REPO_ROOT}/trusttunnel/include
        ${TRUSTTUNNEL_REPO_ROOT}/trusttunnel/src
        ${TRUSTTUNNEL_REPO_ROOT}/common/include
        ${TRUSTTUNNEL_REPO_ROOT}/core/include
        ${TRUSTTUNNEL_REPO_ROOT}/net/include
        ${TRUSTTUNNEL_REPO_ROOT}/tcpip/include
    )
endif()

set_target_properties(trusttunnel-qt PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "TrustTunnel Qt"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.trusttunnel.qtclient"
)

add_custom_command(TARGET trusttunnel-qt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:trusttunnel-qt-helper>
        $<TARGET_FILE_DIR:trusttunnel-qt>/trusttunnel-qt-helper
)
