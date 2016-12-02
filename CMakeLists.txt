#
# Copyright 2016 James S. Waller
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

project(wrparse CXX C)
cmake_minimum_required(VERSION 2.6)

include(CheckCXXSourceRuns)

set(WRPARSE_VERSION_MAJOR 0)
set(WRPARSE_VERSION_MINOR 1)
set(WRPARSE_VERSION_PATCH 0)

if (NOT TOP_BUILD_DIR)
        set(TOP_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
endif()

#########################################
#
# Dependencies
#

#
# Check for wrUtil library
#
if (WRUTIL_ROOT)
        # fall through
elseif (IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/wrutil/)
        set(WRUTIL_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/wrutil)
        file(MAKE_DIRECTORY ${WRUTIL_BUILD_DIR})
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/wrutil ${WRUTIL_BUILD_DIR})

        include(${CMAKE_CURRENT_SOURCE_DIR}/wrutil/cmake/compiler-config.cmake)
        include(${WRUTIL_BUILD_DIR}/wrutil-config-internal.cmake)
endif()

if (NOT WRUTIL_BUILD_DIR)
        if (WRUTIL_ROOT)
                include(${WRUTIL_ROOT}/share/wrutil/compiler-config.cmake)
                include(${WRUTIL_ROOT}/share/wrutil/wrutil-config.cmake)
                include(${WRUTIL_ROOT}/share/wrutil/wrutil-targets.cmake)

                # for check_cxx_*() functions
                set(CMAKE_REQUIRED_LIBRARIES wrutil)
                set(CMAKE_REQUIRED_DEFINITIONS "-Dwrutil_IMPORTS")

                set(CHECK_CXX_CODE "#include <wrutil/uiostream.h>\nint main() { wr::uout << \"Hello\" << std::endl\; return 0\; }\n")
                check_cxx_source_runs(${CHECK_CXX_CODE} HAVE_WRUTIL)
        endif()
        if (NOT HAVE_WRUTIL)
                message(SEND_ERROR "wrutil library not found. Please either specify -DWRUTIL_ROOT=<dir> where <dir> is the base directory of a pre-installed wrutil package, or place a copy of the wrutil source tree (with directory name \"wrutil\") under the top directory of wrparse.")
        endif()
endif()

#
# Check for PCRE2 library
#
set(CMAKE_REQUIRED_DEFINITIONS ${CXX_CHOSEN_STD_FLAG})

IF (PCRE2_ROOT)
        file(TO_CMAKE_PATH "${PCRE2_ROOT}" PCRE2_ROOT)
endif()

if (PCRE2_INCLUDE)
        file(TO_CMAKE_PATH "${PCRE2_INCLUDE}" PCRE2_INCLUDE)
else()
        if (PCRE2_ROOT)
                set(PCRE2_INCLUDE "${PCRE2_ROOT}/include")
        endif()
endif()

if (PCRE2_INCLUDE)
        include_directories(${PCRE2_INCLUDE})
        set(CMAKE_REQUIRED_DEFINITIONS "${CMAKE_REQUIRED_DEFINITIONS} -I${PCRE2_INCLUDE}")
endif()

if (PCRE2_LIB)
        file(TO_CMAKE_PATH "${PCRE2_LIB}" PCRE2_LIB)
else()
        if (PCRE2_ROOT)
                set(PCRE2_LIB "${PCRE2_ROOT}/lib")
        endif()
endif()

if (PCRE2_LIB)
        link_directories(${PCRE2_LIB})
        find_library(PCRE2_LIB_NAME pcre2-8 PATHS "${PCRE2_LIB}")
else()
        find_library(PCRE2_LIB_NAME pcre2-8)
endif()

if (PCRE2_LIB_NAME)
        set(CMAKE_REQUIRED_LIBRARIES "${PCRE2_LIB_NAME}")
        set(CHECK_CXX_CODE "#define PCRE2_CODE_UNIT_WIDTH 8\n#include <pcre2.h>\nint main() { pcre2_code *regex = pcre2_compile((PCRE2_SPTR8) \"\", 0, 0, nullptr, nullptr, nullptr)\; (void) regex\; return 0\; }\n")

        check_cxx_source_runs(${CHECK_CXX_CODE} HAVE_PCRE2)
                # use check_cxx_source_runs() to verify no link errors

        if (WIN32)
                if (NOT HAVE_PCRE2)
                        message("    (Retrying with PCRE2_STATIC defined)")
                        unset(HAVE_PCRE2 CACHE)
                        set(CMAKE_REQUIRED_DEFINITIONS "${CMAKE_REQUIRED_DEFINITIONS} -DPCRE2_STATIC")
                        check_cxx_source_runs(${CHECK_CXX_CODE} HAVE_PCRE2)
                        if (HAVE_PCRE2)
                                add_definitions(-DPCRE2_STATIC)
                        endif()
                endif()
        endif()
else()
        message("PCRE2 library not found.")
endif()

if (NOT HAVE_PCRE2)
        message(SEND_ERROR "PCRE2 dependency not satisfied.\nPlease either specify -DPCRE2_ROOT=<dir> where <dir> is the base directory of your PCRE2 installation, or specify -DPCRE2_INCLUDE=<inc_dir> and -DPCRE2_LIB=<lib_dir> where <inc_dir> and <lib_dir> are the directories containing the PCRE2 headers and libraries respectively.\nIf using shared PCRE2 library (DLL) please ensure your environment is set to pick it up when executing a program linked to it.")
endif()

unset(CMAKE_REQUIRED_DEFINITIONS)
unset(CMAKE_REQUIRED_LIBRARIES)

########################################
#
# Target Definitions
#
include_directories(include)

set(WRPARSE_SOURCES
        src/Diagnostics.cxx
        src/Grammar.cxx
        src/Lexer.cxx
        src/Parser.cxx
        src/PatternLexer.cxx
        src/SPPF.cxx
        src/Token.cxx
)

set(WRPARSE_HEADERS
        include/wrparse/Config.h
        include/wrparse/Diagnostics.h
        include/wrparse/Grammar.h
        include/wrparse/Lexer.h
        include/wrparse/Parser.h
        include/wrparse/PatternLexer.h
        include/wrparse/SPPF.h
        include/wrparse/SPPFOutput.h
        include/wrparse/Token.h
)

add_library(wrparse SHARED ${WRPARSE_SOURCES} ${WRPARSE_HEADERS})
add_library(wrparse_static STATIC ${WRPARSE_SOURCES} ${WRPARSE_HEADERS})

target_link_libraries(wrparse wrutil pcre2-8)

set_target_properties(wrparse PROPERTIES
        COMPILE_FLAGS "-Dwrutil_IMPORTS ${WR_SOFLAGS}"
        SOVERSION ${WRPARSE_VERSION_MAJOR}
        VERSION ${WRPARSE_VERSION_MAJOR}.${WRPARSE_VERSION_MINOR}.${WRPARSE_VERSION_PATCH}
)

add_executable(calc example/calc.cxx)
target_link_libraries(calc wrparse wrutil)
set_target_properties(calc
        PROPERTIES COMPILE_FLAGS "-Dwrutil_IMPORTS -Dwrparse_IMPORTS"
)

########################################
#
# Output Directories
#
set_target_properties(wrparse wrparse_static
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${TOP_BUILD_DIR}/bin
        ARCHIVE_OUTPUT_DIRECTORY ${TOP_BUILD_DIR}/lib
        LIBRARY_OUTPUT_DIRECTORY ${TOP_BUILD_DIR}/lib
)

set_target_properties(calc PROPERTIES RUNTIME_OUTPUT_DIRECTORY example)

########################################
#
# Installation
#
install(TARGETS wrparse wrparse_static
        EXPORT wrparse-targets
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
)

install(DIRECTORY include/wrparse
        DESTINATION include
        DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                              GROUP_READ GROUP_EXECUTE
                              WORLD_READ WORLD_EXECUTE
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE
                         GROUP_READ
                         WORLD_READ
)

install(EXPORT wrparse-targets DESTINATION share/wrparse)

configure_file(cmake/wrparse-config.cmake.in wrparse-config.cmake @ONLY)
configure_file(cmake/wrparse-config-internal.cmake.in
               wrparse-config-internal.cmake @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wrparse-config.cmake
        DESTINATION share/wrparse
        PERMISSIONS OWNER_READ OWNER_WRITE
                    GROUP_READ
                    WORLD_READ
)
