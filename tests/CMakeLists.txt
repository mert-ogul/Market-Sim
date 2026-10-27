include(FetchContent)
find_package(Threads REQUIRED)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(marketsim_tests
  test_smoke.cpp
  test_order_book.cpp
  test_matching_engine.cpp
  test_spsc_ring.cpp
  test_gbm_feed.cpp
  test_portfolio_risk.cpp
  test_gateway_parse.cpp
  test_gateway_integration.cpp
  test_dashboard_ui_model.cpp
  test_symbols_config.cpp
  test_runtime_integration.cpp
  test_market_maker.cpp
  ${MARKETSIM_GATEWAY_SOURCES}
)
target_include_directories(marketsim_tests SYSTEM PRIVATE ${httplib_SOURCE_DIR})
target_link_libraries(marketsim_tests PRIVATE
  marketsim::runtime
  marketsim::ui_model
  GTest::gtest_main
  Threads::Threads
)
marketsim_apply_warnings(marketsim_tests PRIVATE)

if(MARKETSIM_SANITIZE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  marketsim_enable_sanitizers(marketsim_tests)
endif()

include(GoogleTest)
gtest_discover_tests(marketsim_tests)
