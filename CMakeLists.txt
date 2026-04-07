cmake_minimum_required(VERSION 3.16)
project(svf-tree-sitter VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------- SVF ----------------
if(NOT DEFINED SVF_DIR)
    message(FATAL_ERROR "Pass -DSVF_DIR=/path/to/SVF (the SVF source root, "
                        "containing svf/include and Release-build/svf/libSvfCore.a)")
endif()

set(SVF_INCLUDE_DIR ${SVF_DIR}/svf/include)
set(SVF_BUILD_INCLUDE_DIR ${SVF_DIR}/Release-build/include)
find_library(SVF_CORE_LIB
    NAMES SvfCore libSvfCore
    HINTS ${SVF_DIR}/Release-build/svf ${SVF_DIR}/Release-build/lib
          ${SVF_DIR}/Debug-build/svf  ${SVF_DIR}/Debug-build/lib
    NO_DEFAULT_PATH)
if(NOT SVF_CORE_LIB)
    message(FATAL_ERROR "Could not find libSvfCore under ${SVF_DIR}")
endif()
message(STATUS "SVF include: ${SVF_INCLUDE_DIR}")
message(STATUS "SVF core lib: ${SVF_CORE_LIB}")

add_library(SvfCore STATIC IMPORTED)
set_target_properties(SvfCore PROPERTIES
    IMPORTED_LOCATION ${SVF_CORE_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${SVF_INCLUDE_DIR};${SVF_BUILD_INCLUDE_DIR}")

# Optional Z3 (SVF often links Z3)
find_library(Z3_LIB z3 HINTS ${SVF_DIR}/z3.obj/bin)

# ---------------- tree-sitter ----------------
add_library(tree-sitter STATIC
    third_party/tree-sitter/lib/src/lib.c)
target_include_directories(tree-sitter PUBLIC
    third_party/tree-sitter/lib/include
    third_party/tree-sitter/lib/src)
target_compile_options(tree-sitter PRIVATE -w)

add_library(tree-sitter-c STATIC
    third_party/tree-sitter-c/src/parser.c)
target_include_directories(tree-sitter-c PUBLIC
    third_party/tree-sitter-c/src)
target_link_libraries(tree-sitter-c PUBLIC tree-sitter)
target_compile_options(tree-sitter-c PRIVATE -w)

# ---------------- svf-ts-core ----------------
add_library(svf-ts-core STATIC
    src/SimpleDataLayout.cpp
    src/TSTypeBuilder.cpp
    src/TSSymbolTable.cpp
    src/TSICFGBuilder.cpp
    src/TSIRBuilder.cpp
    src/GepHandler.cpp
    src/SVFIRVerifier.cpp
    src/TSFrontend.cpp)
target_include_directories(svf-ts-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(svf-ts-core PUBLIC SvfCore tree-sitter tree-sitter-c)
if(Z3_LIB)
    target_link_libraries(svf-ts-core PUBLIC ${Z3_LIB})
endif()
target_compile_options(svf-ts-core PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(ts-svf src/main.cpp)
target_link_libraries(ts-svf PRIVATE svf-ts-core)
