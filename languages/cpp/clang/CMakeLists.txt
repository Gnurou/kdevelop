include_directories(
    ${KDevelop_SOURCE_DIR}/languages/cpp
    ${KDevelop_SOURCE_DIR}/languages/cpp/parser
    ${KDevelop_SOURCE_DIR}/languages/cpp/cppduchain
)

add_definitions(-I/usr/include  -DNDEBUG -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -O3 -fomit-frame-pointer -fvisibility-inlines-hidden -fno-exceptions -fPIC -Woverloaded-virtual)

set(kdevclang_SRCS
    clangparsejob.cpp
)

set(CLANGLIBS
    clang
)

link_directories(/usr/lib/llvm)
kde4_add_library(kdevclang STATIC ${kdevclang_SRCS})
target_link_libraries(kdevclang ${CLANGLIBS})
