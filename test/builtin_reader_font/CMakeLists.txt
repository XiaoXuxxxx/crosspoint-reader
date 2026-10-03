add_executable(BuiltinReaderFontTest
  BuiltinReaderFontTest.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(BuiltinReaderFontTest PRIVATE
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(BuiltinReaderFontTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

if(NOT MSVC)
  # Generated EpdFontData aggregates intentionally omit zero-initialized runtime handlers.
  target_compile_options(BuiltinReaderFontTest PRIVATE -Wno-missing-field-initializers)
endif()

gtest_discover_tests(BuiltinReaderFontTest)
