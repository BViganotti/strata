cmake_minimum_required(VERSION 3.16)
project(strata VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

# The web UI is compiled into the binary so `strata serve` needs no asset
# directory at runtime.
set(WEB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/web)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(WEB_ASSETS_CPP ${GEN_DIR}/web_assets.cpp)
file(GLOB_RECURSE WEB_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false ${WEB_DIR}/*)

add_custom_command(
  OUTPUT ${WEB_ASSETS_CPP}
  COMMAND ${CMAKE_COMMAND}
          -DINPUT_DIR=${WEB_DIR}
          -DOUTPUT=${WEB_ASSETS_CPP}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web.cmake
  DEPENDS ${WEB_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_web.cmake
  COMMENT "Embedding web assets"
  VERBATIM)

add_library(strata_core STATIC
  src/git.cpp
  src/lang.cpp
  src/graph.cpp
  src/analyze.cpp
  src/config.cpp
  src/store.cpp
  src/check.cpp
  src/web.cpp
  ${WEB_ASSETS_CPP})

target_include_directories(strata_core PUBLIC src)
target_include_directories(strata_core SYSTEM PUBLIC third_party ${GEN_DIR})
target_compile_options(strata_core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(strata_core PRIVATE STRATA_VERSION="${PROJECT_VERSION}")
target_link_libraries(strata_core PUBLIC SQLite::SQLite3 Threads::Threads)

add_executable(strata cmd/strata/main.cpp)
target_link_libraries(strata PRIVATE strata_core)
target_compile_options(strata PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(strata PRIVATE STRATA_VERSION="${PROJECT_VERSION}")

include(GNUInstallDirs)
install(TARGETS strata RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
  add_executable(strata_tests tests/unit.cpp)
  target_link_libraries(strata_tests PRIVATE strata_core)
  add_test(NAME unit COMMAND strata_tests)
  add_test(NAME integration
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.sh $<TARGET_FILE:strata>)
endif()
