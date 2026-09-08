# util/CMakeLists.txt
add_library(fbjq-util util.cpp config.cpp dirent.cpp systemd.cpp)

target_compile_definitions(fbjq-util PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    _FILE_OFFSET_BITS=64
    _GNU_SOURCE
)

target_include_directories(fbjq-util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# libconfig++
pkg_check_modules(LIBCONFIG_CPP REQUIRED IMPORTED_TARGET libconfig++)
target_link_libraries(fbjq-util PUBLIC PkgConfig::LIBCONFIG_CPP)

# sdbus-c++
find_package(sdbus-c++ REQUIRED)
target_link_libraries(fbjq-util PUBLIC SDBusCpp::sdbus-c++)

# print
get_target_property(FBJQ_UTIL_LIBS fbjq-util LINK_LIBRARIES)
message(STATUS "fbjq-util link libs = ${FBJQ_UTIL_LIBS}")

# == INSTALL SECTION ==>>
install(TARGETS fbjq-util
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} # .a ファイル用 (STATIC)
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} # .so ファイル用 (SHARED)
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} # .dll 用 (Windows環境考慮)
)
# == INSTALL SECTION <<==
