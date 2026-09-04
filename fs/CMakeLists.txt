# fs/CMakeLists.txt
add_executable(fbjq-fs main.cpp operation.cpp)

target_compile_definitions(fbjq-fs PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    _FILE_OFFSET_BITS=64
    _GNU_SOURCE
)

# fuse3
pkg_check_modules(FUSE3 REQUIRED IMPORTED_TARGET fuse3)
target_link_libraries(fbjq-fs PRIVATE PkgConfig::FUSE3)
target_compile_definitions(fbjq-fs PRIVATE FUSE_USE_VERSION=31)

# fbjq-lib
target_link_libraries(fbjq-fs PRIVATE fbjq-lib)

# print
get_target_property(FBJQ_FS_LIBS fbjq-fs LINK_LIBRARIES)
message(STATUS "fbjq-fs link libs = ${FBJQ_FS_LIBS}")
