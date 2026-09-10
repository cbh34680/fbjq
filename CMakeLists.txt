# CMakeLists.txt
cmake_minimum_required(VERSION 3.30)
project(fbjq CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    $<$<CONFIG:Debug>:-fsanitize=address,leak,undefined>
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
)

add_link_options(
    $<$<CONFIG:Debug>:-fsanitize=address,leak,undefined>
)

find_package(PkgConfig REQUIRED)

# 現在設定されているフラグを確認・出力する例
message(STATUS "CXX Flags (Debug): ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CXX Flags (Release): ${CMAKE_CXX_FLAGS_RELEASE}")

# == INSTALL SECTION ==>>
# GNUInstallDirs の前のデフォルトパス調整
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_SYSCONFDIR "/etc" CACHE PATH "system config dir")
    set(CMAKE_INSTALL_RUNSTATEDIR "/run" CACHE PATH "run state dir")
    set(CMAKE_INSTALL_LOCALSTATEDIR "/var" CACHE PATH "local state dir")
endif()

if(NOT DEFINED SYSTEMD_UNIT_DIR)
    set(SYSTEMD_UNIT_DIR "/lib/systemd/system" CACHE PATH "systemd unit dir")
endif()

include(GNUInstallDirs)

# 標準的なパーミッション定義（ディレクトリ: 755 / ファイル: 644）
set(STD_DIR_PERM
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE
)
set(STD_FILE_PERM
    OWNER_READ OWNER_WRITE
    GROUP_READ
    WORLD_READ
)

# 1. /etc 配下 (deploy/etc/ -> /etc/)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deploy/etc/
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}
    DIRECTORY_PERMISSIONS ${STD_DIR_PERM}
    FILE_PERMISSIONS ${STD_FILE_PERM}
)

# 2. systemd ユニットファイル (deploy/lib/systemd/system/ -> /lib/systemd/system/)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deploy/lib/systemd/system/
    DESTINATION ${SYSTEMD_UNIT_DIR}
    DIRECTORY_PERMISSIONS ${STD_DIR_PERM}
    FILE_PERMISSIONS ${STD_FILE_PERM}
    FILES_MATCHING
    PATTERN "*.service"
    PATTERN "*.path"
)

# 3. 実行時状態・スプールディレクトリの作成
#install(DIRECTORY DESTINATION ${CMAKE_INSTALL_RUNSTATEDIR}/fbjq
#    DIRECTORY_PERMISSIONS ${STD_DIR_PERM}
#)
#install(DIRECTORY DESTINATION ${CMAKE_INSTALL_LOCALSTATEDIR}/spool/fbjq
#    DIRECTORY_PERMISSIONS ${STD_DIR_PERM}
#)
# == INSTALL SECTION <<==

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/a/CMakeLists.txt")
    add_subdirectory(a)
endif()

add_subdirectory(util)
add_subdirectory(fs)
add_subdirectory(delivery)
add_subdirectory(executor)

# 全ファイルの配置後に systemd-tmpfiles を実行してディレクトリ構成を整える
install(CODE "
    execute_process(
        COMMAND systemd-tmpfiles --create /etc/tmpfiles.d/fbjq-setup.conf
        RESULT_VARIABLE TMPFILES_RET
    )
    if(TMPFILES_RET EQUAL 0)
        message(STATUS \"Successfully created systemd-tmpfiles entries for fbjq-setup.conf\")
    else()
        message(WARNING \"Failed to run systemd-tmpfiles (exit code: \${TMPFILES_RET})\")
    endif()
")
