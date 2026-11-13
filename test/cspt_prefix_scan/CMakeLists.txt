add_executable(CsptPrefixScanTest
  CsptPrefixScanTest.cpp
  ${REPO_ROOT}/src/util/Dictionary.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(CsptPrefixScanTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(CsptPrefixScanTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CsptPrefixScanTest)
