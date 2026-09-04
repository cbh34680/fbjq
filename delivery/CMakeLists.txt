# delivery/CMakeLists.txt

add_executable(fbjq-delivery main.cpp)

target_compile_definitions(fbjq-delivery PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    _FILE_OFFSET_BITS=64
    _GNU_SOURCE
)

# fuse3
pkg_check_modules(FUSE3 REQUIRED IMPORTED_TARGET fuse3)
target_link_libraries(fbjq-delivery PRIVATE PkgConfig::FUSE3)
target_compile_definitions(fbjq-delivery PRIVATE FUSE_USE_VERSION=31)

# fbjq-lib
target_link_libraries(fbjq-delivery PRIVATE fbjq-lib)

# print
get_target_property(FBJQ_DELIVERY_LIBS fbjq-delivery LINK_LIBRARIES)
message(STATUS "fbjq-delivery link libs = ${FBJQ_DELIVERY_LIBS}")
