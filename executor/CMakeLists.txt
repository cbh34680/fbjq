#/executor/CMakeLists.txt

add_executable(fbjq-executor main.cpp)

target_compile_definitions(fbjq-executor PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    _FILE_OFFSET_BITS=64
    _GNU_SOURCE
)

# Threads
find_package(Threads REQUIRED)
target_link_libraries(fbjq-executor PRIVATE Threads::Threads)

# libconfig++
pkg_check_modules(LIBCONFIG_CPP REQUIRED IMPORTED_TARGET libconfig++)
target_link_libraries(fbjq-executor PRIVATE PkgConfig::LIBCONFIG_CPP)

# print
get_target_property(FBJQ_EXECUTOR_INCLUDES fbjq-executor INCLUDE_DIRECTORIES)
message(STATUS "fbjq-executor include dirs = ${FBJQ_EXECUTOR_INCLUDES}")

get_target_property(FBJQ_EXECUTOR_LIBS fbjq-executor LINK_LIBRARIES)
message(STATUS "fbjq-executor link libs = ${FBJQ_EXECUTOR_LIBS}")

get_target_property(FBJQ_EXECUTOR_CFLAGS fbjq-executor COMPILE_OPTIONS)
message(STATUS "fbjq-executor cflags = ${FBJQ_EXECUTOR_CFLAGS}")
