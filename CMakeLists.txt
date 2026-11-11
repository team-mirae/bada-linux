cmake_minimum_required(VERSION 3.21)
file(STRINGS VERSION BADA_LINUX_VERSION LIMIT_COUNT 1)
project(bada-linux VERSION ${BADA_LINUX_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

option(BADA_FCITX5 "Build the Fcitx5 addon" ON)
option(BADA_IBUS "Build the IBus engine" ON)
option(BADA_TESTS "Build the core tests" ON)
option(BADA_WERROR "Warnings are errors" OFF)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)

add_compile_options(-Wall -Wextra)
if(BADA_WERROR)
  add_compile_options(-Werror)
endif()
add_compile_definitions(BADA_LINUX_VERSION="${PROJECT_VERSION}")

# The framework-free core: engine loading, keys, the host and session logic, settings.
add_library(bada_core STATIC
  src/core/engine.cpp src/core/host.cpp src/core/json.cpp src/core/keys.cpp
  src/core/manifest.cpp src/core/output.cpp src/core/session.cpp src/core/settings.cpp)
target_include_directories(bada_core PUBLIC src/core)
target_link_libraries(bada_core PUBLIC ${CMAKE_DL_LIBS})

# What both frontends share: finding manifests, publishing the state.
add_library(bada_common STATIC src/common/discover.cpp src/common/publish.cpp)
target_include_directories(bada_common PUBLIC src/common)
target_link_libraries(bada_common PUBLIC bada_core PkgConfig::DBUS)

if(BADA_FCITX5)
  find_package(Fcitx5Core 5.0 REQUIRED)
  add_library(bada-linux MODULE src/fcitx5/bada_fcitx5.cpp)
  target_link_libraries(bada-linux PRIVATE Fcitx5::Core bada_common)
  set_target_properties(bada-linux PROPERTIES PREFIX "lib")
  set(BADA_FCITX5_ADDONDIR "${CMAKE_INSTALL_LIBDIR}/fcitx5" CACHE STRING "Where Fcitx5 loads addons from")
  configure_file(src/fcitx5/bada-linux.conf.in bada-linux.conf @ONLY)
  install(TARGETS bada-linux DESTINATION ${BADA_FCITX5_ADDONDIR})
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bada-linux.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/fcitx5/addon)
endif()

if(BADA_IBUS)
  pkg_check_modules(IBUS REQUIRED IMPORTED_TARGET ibus-1.0)
  add_executable(bada-ibus src/ibus/bada_ibus.cpp)
  target_link_libraries(bada-ibus PRIVATE PkgConfig::IBUS bada_common)
  target_compile_definitions(bada-ibus PRIVATE BADA_IBUS_EXEC="${CMAKE_INSTALL_FULL_LIBEXECDIR}/bada-ibus")
  install(TARGETS bada-ibus DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
endif()

install(PROGRAMS tools/install-ime.sh DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME bada-linux-install-ime)

if(BADA_TESTS)
  enable_testing()
  add_executable(bl_core_tests tests/core_tests.cpp)
  target_link_libraries(bl_core_tests PRIVATE bada_common)
  add_test(NAME core COMMAND bl_core_tests)
endif()
