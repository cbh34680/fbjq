# delivery/CMakeLists.txt

add_executable(fbjq-delivery main.cpp delivery.cpp)

target_compile_definitions(fbjq-delivery PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    _FILE_OFFSET_BITS=64
    _GNU_SOURCE
)

# fbjq-util
target_link_libraries(fbjq-delivery PRIVATE fbjq-util)

# print
get_target_property(FBJQ_DELIVERY_LIBS fbjq-delivery LINK_LIBRARIES)
message(STATUS "fbjq-delivery link libs = ${FBJQ_DELIVERY_LIBS}")

# == INSTALL SECTION ==>>
install(TARGETS fbjq-delivery
    RUNTIME
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE
)
# == INSTALL SECTION <<==
