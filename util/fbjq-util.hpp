// util/fbjq-util.hpp
#pragma once

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <ostream>
#include <source_location>
#include <string>
#include <type_traits>
#include <grp.h>
#include <pwd.h>
#include <sys/syscall.h>
#include <libconfig.h++>

namespace fbjqutil {

struct [[nodiscard]] restore_errno_t_
{
    const int save_errno_;
    restore_errno_t_() : save_errno_{ errno } { errno = 0; }
    ~restore_errno_t_() { errno = save_errno_; }
};

template <typename... Args>
void log_impl(const char* level, std::ostream& os, const std::source_location& loc,
    std::format_string<Args...> fmt, Args&&... args)
{
    restore_errno_t_ restore_errno_1__;

    try {
        char buf[512];

        auto result = std::format_to_n(buf, sizeof(buf),
            "{}: {}({}): {}: errno={}: ", level, loc.file_name(),loc.line(),loc.function_name(),
            restore_errno_1__.save_errno_);

        const auto prefix_size = static_cast<size_t>(result.out - buf);

        if (prefix_size >= sizeof(buf)) {
            os.write(buf, sizeof(buf));
            os.put('\n');
            return;
        }

        result = std::format_to_n(buf + prefix_size,
            sizeof(buf) - prefix_size, fmt, std::forward<Args>(args)...);

        const auto total_size = static_cast<size_t>(result.out - buf);

        os.write(buf, total_size);
        os.put('\n');
    }
    catch (...) {
        // ignore
    }
}

inline pid_t gettid() {
    return static_cast<pid_t>(::syscall(SYS_gettid));
}

inline constexpr const char* DEFAULT_CONFIG_FILE = "/etc/fbjq.conf";
inline constexpr gid_t DEFAULT_FILE_GROUP = static_cast<gid_t>(0);
inline constexpr int QUEUE_MAX_PROCESS = 32;
inline constexpr int QUEUE_NAME_MAXLEN = 31;

struct queue_item_view_t
{
    // exec_user / allow_group は Setting の内部バッファを指す生ポインタ。
    // 取得元の Config が生きている短いスコープでのみ使用すること。
    const char* exec_user{ nullptr };
    const char* allow_group{ nullptr };
    uid_t exec_user_uid{ static_cast<uid_t>(-1) };
    gid_t allow_group_gid{ static_cast<gid_t>(-1) };
    int max_process{ 1 };
};

// util/util.cpp
std::int64_t now_nanos();
bool get_path_from_fd(int fd, char* buf, size_t buf_siz);
bool get_uid_by_name(const char* user_name, uid_t* out_uid);
bool get_gid_by_name(const char* group_name, gid_t* out_gid);

// util/config.cpp
std::unique_ptr<libconfig::Config> load_config(const char* cfg_file);
bool get_queue_item(const libconfig::Config* app_cfg, const char* name, queue_item_view_t* queue_item);
int for_each_queue_item(const libconfig::Config* app_cfg, std::function<bool(const char* q_name, const queue_item_view_t& q_item)> callback);

// util/systemd.cpp
bool systemd_unit_method(const std::string& unit_name, const char* method);

inline bool systemctl_start_unit(const std::string& unit_name) {
    return systemd_unit_method(unit_name, "StartUnit");
}

inline bool systemctl_stop_unit(const std::string& unit_name) {
    return systemd_unit_method(unit_name, "StopUnit");
}

struct request_header_t
{
    char magic[4];
    char version[4];
    std::uint32_t client_uid;
    std::uint32_t client_gid;
    std::int32_t client_pid;
    std::int32_t fuse_pid;
    std::int32_t fuse_tid;
    std::uint32_t exec_user_uid;
    std::uint32_t allow_group_gid;
    char padding1[4];
    char q_name[QUEUE_NAME_MAXLEN + 1];
    char padding2[52];
    char cigam[4];
};

static_assert(sizeof(request_header_t) == 128, "Header size must be 128 bytes");
static_assert(std::is_trivially_copyable_v<request_header_t>, "Header must be trivial");

} // namespace fbjqutil

#define LOG_ERROR(...) fbjqutil::log_impl("ERR", std::cerr, std::source_location::current(), __VA_ARGS__)

#define LOG_INFO(...) fbjqutil::log_impl("INF", std::cout, std::source_location::current(), __VA_ARGS__)

#if defined(DEBUG)
#define LOG_DEBUG(...) fbjqutil::log_impl("DBG", std::cerr, std::source_location::current(), __VA_ARGS__)
#else
#define LOG_DEBUG(...) do { } while (false)
#endif

#define ENTER_FUNCTION() fbjqutil::restore_errno_t_ restore_errno_2__; LOG_DEBUG("ENTER")
