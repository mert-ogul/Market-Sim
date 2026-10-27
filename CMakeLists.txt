cmake_minimum_required(VERSION 3.20)

project(marketsim
  VERSION 0.1.0
  DESCRIPTION "Real-time stock market simulator (C++20)"
  LANGUAGES CXX
)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MARKETSIM_WERROR "Treat warnings as errors" ON)
option(MARKETSIM_SANITIZE "Enable ASan/UBSan in Debug builds (Clang/GCC)" ON)

include(cmake/CompilerWarnings.cmake)

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

set(MARKETSIM_SIMULATOR_SERVICE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/simulator_service.cpp")
set(MARKETSIM_GATEWAY_SOURCES
  "${MARKETSIM_SIMULATOR_SERVICE_SRC}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/gateway_order_parse.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/gateway_http_register.cpp"
)

add_library(marketsim_ui_model STATIC
  src/ui/dashboard_format.cpp
  src/ui/dashboard_prepare.cpp
)
add_library(marketsim::ui_model ALIAS marketsim_ui_model)
target_include_directories(marketsim_ui_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(marketsim_ui_model PUBLIC cxx_std_20)
marketsim_apply_warnings(marketsim_ui_model PRIVATE)

add_library(marketsim_core STATIC
  src/order_book.cpp
  src/matching_engine.cpp
  src/gbm_feed.cpp
  src/portfolio.cpp
  src/risk_manager.cpp
)
add_library(marketsim::core ALIAS marketsim_core)
target_include_directories(marketsim_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(marketsim_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(marketsim_core PUBLIC cxx_std_20)
marketsim_apply_warnings(marketsim_core PUBLIC)

add_library(marketsim_runtime STATIC
  src/symbols_config.cpp
  src/simulator_runtime.cpp
)
add_library(marketsim::runtime ALIAS marketsim_runtime)
target_include_directories(marketsim_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(marketsim_runtime PUBLIC marketsim::core Threads::Threads)
target_compile_features(marketsim_runtime PUBLIC cxx_std_20)
marketsim_apply_warnings(marketsim_runtime PRIVATE)

add_library(marketsim_dashboard_driver STATIC
  src/ui/dashboard_app.cpp
  src/ui/dashboard_layout.cpp
)
add_library(marketsim::dashboard_driver ALIAS marketsim_dashboard_driver)
target_include_directories(marketsim_dashboard_driver PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(marketsim_dashboard_driver PUBLIC
  marketsim::core
  marketsim::ui_model
  ftxui::component
  ftxui::dom
  ftxui::screen
  Threads::Threads
)
target_compile_features(marketsim_dashboard_driver PUBLIC cxx_std_20)
marketsim_apply_warnings(marketsim_dashboard_driver PRIVATE)

add_executable(marketsim src/main.cpp)
target_link_libraries(marketsim PRIVATE marketsim::core)

add_executable(marketsim_server
  src/marketsim_server.cpp
  ${MARKETSIM_GATEWAY_SOURCES}
)
target_include_directories(marketsim_server SYSTEM PRIVATE ${httplib_SOURCE_DIR})
target_link_libraries(marketsim_server PRIVATE marketsim::runtime Threads::Threads)
marketsim_apply_warnings(marketsim_server PRIVATE)

add_executable(marketsim_dashboard
  src/marketsim_dashboard.cpp
  ${MARKETSIM_SIMULATOR_SERVICE_SRC}
)
target_link_libraries(marketsim_dashboard PRIVATE
  marketsim::runtime
  marketsim::dashboard_driver
  Threads::Threads
)
marketsim_apply_warnings(marketsim_dashboard PRIVATE)

add_executable(marketsim_demo
  src/marketsim_demo.cpp
  ${MARKETSIM_GATEWAY_SOURCES}
)
target_include_directories(marketsim_demo SYSTEM PRIVATE ${httplib_SOURCE_DIR})
target_link_libraries(marketsim_demo PRIVATE
  marketsim::runtime
  marketsim::dashboard_driver
  Threads::Threads
)
marketsim_apply_warnings(marketsim_demo PRIVATE)
target_compile_definitions(marketsim PRIVATE
  "MARKETSIM_VERSION=\"${PROJECT_VERSION}\""
)
marketsim_apply_warnings(marketsim PRIVATE)

if(MARKETSIM_SANITIZE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  marketsim_enable_sanitizers(marketsim)
  marketsim_enable_sanitizers(marketsim_server)
  marketsim_enable_sanitizers(marketsim_dashboard)
  marketsim_enable_sanitizers(marketsim_demo)
endif()

enable_testing()
add_subdirectory(tests)
