# lib/CMakeLists.txt
add_library(fbjq-lib lib.cpp)

target_compile_definitions(fbjq-lib PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    _FILE_OFFSET_BITS=64
    _GNU_SOURCE
)

target_include_directories(fbjq-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# libconfig++
pkg_check_modules(LIBCONFIG_CPP REQUIRED IMPORTED_TARGET libconfig++)
target_link_libraries(fbjq-lib PUBLIC PkgConfig::LIBCONFIG_CPP)

# sdbus-c++
find_package(sdbus-c++ REQUIRED)
target_link_libraries(fbjq-lib PUBLIC SDBusCpp::sdbus-c++)

# print
get_target_property(FBJQ_LIB_LIBS fbjq-lib LINK_LIBRARIES)
message(STATUS "fbjq-lib link libs = ${FBJQ_LIB_LIBS}")
