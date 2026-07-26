find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(THAI_LEXICON_GENERATED ${REPO_ROOT}/lib/Epub/Epub/thai/generated/thaiLexicon.generated.h)
set(THAI_LEXICON_SCRIPT ${REPO_ROOT}/scripts/gen_thai_lexicon.py)
set(THAI_LEXICON_WORDS ${REPO_ROOT}/data/lexicon/words_th.txt)
set(THAI_LEXICON_FREQUENCIES ${REPO_ROOT}/data/lexicon/tnc_freq.txt)

add_custom_command(
  OUTPUT ${THAI_LEXICON_GENERATED}
  COMMAND ${Python3_EXECUTABLE} ${THAI_LEXICON_SCRIPT}
          --input ${THAI_LEXICON_WORDS}
          --freq ${THAI_LEXICON_FREQUENCIES}
          --output ${THAI_LEXICON_GENERATED}
  DEPENDS
    ${THAI_LEXICON_SCRIPT}
    ${THAI_LEXICON_WORDS}
    ${THAI_LEXICON_FREQUENCIES}
  WORKING_DIRECTORY ${REPO_ROOT}
  VERBATIM
)

add_custom_target(Utf8ComposeThaiLexicon DEPENDS ${THAI_LEXICON_GENERATED})

add_executable(Utf8ComposeTest
  Utf8ComposeTest.cpp
  ThaiSegmentationTest.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/Epub/Epub/thai/ThaiSegmenter.cpp
  ${THAI_LEXICON_GENERATED}
)

add_dependencies(Utf8ComposeTest Utf8ComposeThaiLexicon)

target_include_directories(Utf8ComposeTest PRIVATE
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(Utf8ComposeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(Utf8ComposeTest)
