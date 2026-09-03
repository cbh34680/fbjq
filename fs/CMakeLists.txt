# fs/CMakeLists.txt

add_executable(fbjq-fs main.cpp util.cpp handler.cpp)

target_compile_definitions(fbjq-fs PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    _FILE_OFFSET_BITS=64
    _GNU_SOURCE
)

# fuse3
pkg_check_modules(FUSE3 REQUIRED IMPORTED_TARGET fuse3)
target_link_libraries(fbjq-fs PRIVATE PkgConfig::FUSE3)
target_compile_definitions(fbjq-fs PRIVATE FUSE_USE_VERSION=31)

# libconfig++
pkg_check_modules(LIBCONFIG_CPP REQUIRED IMPORTED_TARGET libconfig++)
target_link_libraries(fbjq-fs PRIVATE PkgConfig::LIBCONFIG_CPP)

# sdbus-c++
find_package(sdbus-c++ REQUIRED)
target_link_libraries(fbjq-fs PRIVATE SDBusCpp::sdbus-c++)

# print
get_target_property(FBJQ_FS_INCLUDES fbjq-fs INCLUDE_DIRECTORIES)
message(STATUS "fbjq-fs include dirs = ${FBJQ_FS_INCLUDES}")

get_target_property(FBJQ_FS_LIBS fbjq-fs LINK_LIBRARIES)
message(STATUS "fbjq-fs link libs = ${FBJQ_FS_LIBS}")

get_target_property(FBJQ_FS_CFLAGS fbjq-fs COMPILE_OPTIONS)
message(STATUS "fbjq-fs cflags = ${FBJQ_FS_CFLAGS}")
