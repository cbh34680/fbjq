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

# fbjq-lib
target_link_libraries(fbjq-executor PRIVATE fbjq-lib)

# print
get_target_property(FBJQ_EXECUTOR_INCLUDES fbjq-executor INCLUDE_DIRECTORIES)
message(STATUS "fbjq-executor include dirs = ${FBJQ_EXECUTOR_INCLUDES}")

get_target_property(FBJQ_EXECUTOR_LIBS fbjq-executor LINK_LIBRARIES)
message(STATUS "fbjq-executor link libs = ${FBJQ_EXECUTOR_LIBS}")

get_target_property(FBJQ_EXECUTOR_CFLAGS fbjq-executor COMPILE_OPTIONS)
message(STATUS "fbjq-executor cflags = ${FBJQ_EXECUTOR_CFLAGS}")
