add_executable(CombiningMarkAnchorTest
  CombiningMarkAnchorTest.cpp
)

target_include_directories(CombiningMarkAnchorTest PRIVATE
  ${REPO_ROOT}/lib/EpdFont
)

# The built-in font headers leave EpdFontData's trailing handler fields to zero-initialization.
target_compile_options(CombiningMarkAnchorTest PRIVATE -Wno-missing-field-initializers)

target_link_libraries(CombiningMarkAnchorTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CombiningMarkAnchorTest)
