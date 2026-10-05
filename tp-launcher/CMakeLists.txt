cmake_minimum_required(VERSION 3.16)
project(TPHouseLauncher LANGUAGES CXX)

# House launcher for ReXGlue recomp games. One codebase, per-game
# builds: TP_GAME selects the compiled-in profile, theme and art.
#   maw         (default) -> TPMawLauncher        "TP Maw Launcher"
#   splosionman           -> TPSplosionmanLauncher "TP Splosionman Launcher"
#   realsteel             -> TPFactualMetalLauncher "Factual Metal Launcher"
#   pacificrim            -> TPPacificRimLauncher  "Shatterdome Launcher"
# Qt Widgets, C++20. Verified against Qt 6.4.2 (recomp-dev distrobox)
# and Qt 6.10 (host) - stick to Qt 6.4-era APIs so it builds in both.

set(TP_GAME "maw" CACHE STRING "Game profile to build: maw, splosionman, realsteel or pacificrim")
if(TP_GAME STREQUAL "maw")
    set(TP_TARGET TPMawLauncher)
    set(TP_QRC resources.qrc)
    set(TP_GAME_DEFINE TP_GAME_MAW)
elseif(TP_GAME STREQUAL "splosionman")
    set(TP_TARGET TPSplosionmanLauncher)
    set(TP_QRC resources-splosion.qrc)
    set(TP_GAME_DEFINE TP_GAME_SPLOSIONMAN)
elseif(TP_GAME STREQUAL "realsteel")
    set(TP_TARGET TPFactualMetalLauncher)
    set(TP_QRC resources-realsteel.qrc)
    set(TP_GAME_DEFINE TP_GAME_REALSTEEL)
elseif(TP_GAME STREQUAL "pacificrim")
    set(TP_TARGET TPPacificRimLauncher)
    set(TP_QRC resources-pacificrim.qrc)
    set(TP_GAME_DEFINE TP_GAME_PACIFICRIM)
else()
    message(FATAL_ERROR "TP_GAME must be maw, splosionman, realsteel or pacificrim (got: ${TP_GAME})")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(${TP_TARGET}
    ${TP_QRC}
    src/main.cpp
    src/mainwindow.cpp
    src/archiveimport.cpp
    src/dlcimport.cpp
    src/gamesettings.cpp
    src/gamedetect.cpp
    src/launcher.cpp
    src/platform.cpp
    src/stfs/stfspackage.cpp
)
target_compile_definitions(${TP_TARGET} PRIVATE ${TP_GAME_DEFINE})

target_include_directories(${TP_TARGET} PRIVATE src)
target_link_libraries(${TP_TARGET} PRIVATE Qt6::Widgets)

# src/stfs/ (XBLA/STFS package import) is built into the launcher -
# see the sources above and the stfs_test harness below.
#
# Theming is built in: theme/*.qss + per-game art are embedded via
# the per-game resources*.qrc (AUTORCC); the stylesheet is applied
# in main.cpp from the active profile.

# Headless STFS reader test harness: lists + extracts a package.
option(TPMAW_BUILD_STFS_TEST "Build the stfs_test CLI harness" OFF)
if(TPMAW_BUILD_STFS_TEST)
    add_executable(stfs_test
        src/stfs/stfs_test_main.cpp
        src/stfs/stfspackage.cpp
    )
    target_include_directories(stfs_test PRIVATE src)
    target_link_libraries(stfs_test PRIVATE Qt6::Core)
endif()

# Headless DLC importer test harness: installs packages into a user
# data root from the command line.
option(TP_BUILD_DLC_TEST "Build the dlc_test CLI harness" OFF)
if(TP_BUILD_DLC_TEST)
    add_executable(dlc_test
        src/stfs/dlc_test_main.cpp
        src/dlcimport.cpp
        src/archiveimport.cpp
        src/stfs/stfspackage.cpp
    )
    target_include_directories(dlc_test PRIVATE src)
    target_link_libraries(dlc_test PRIVATE Qt6::Core)
endif()
