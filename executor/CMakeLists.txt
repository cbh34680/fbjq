# executor/CMakeLists.txt
add_executable(fbjq-executor main.cpp executor.cpp)

target_compile_definitions(fbjq-executor PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    _FILE_OFFSET_BITS=64
    _GNU_SOURCE
)

# Threads
find_package(Threads REQUIRED)
target_link_libraries(fbjq-executor PRIVATE Threads::Threads)

# fbjq-util
target_link_libraries(fbjq-executor PRIVATE fbjq-util)

# print
get_target_property(FBJQ_EXECUTOR_LIBS fbjq-executor LINK_LIBRARIES)
message(STATUS "fbjq-executor link libs = ${FBJQ_EXECUTOR_LIBS}")

# == INSTALL SECTION ==>>
install(TARGETS fbjq-executor
    RUNTIME
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE
)
# == INSTALL SECTION <<==
