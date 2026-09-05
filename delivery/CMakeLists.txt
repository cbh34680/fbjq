# delivery/CMakeLists.txt

add_executable(fbjq-delivery main.cpp)

target_compile_definitions(fbjq-delivery PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    _FILE_OFFSET_BITS=64
    _GNU_SOURCE
)

# fbjq-lib
target_link_libraries(fbjq-delivery PRIVATE fbjq-lib)

# print
get_target_property(FBJQ_DELIVERY_LIBS fbjq-delivery LINK_LIBRARIES)
message(STATUS "fbjq-delivery link libs = ${FBJQ_DELIVERY_LIBS}")
