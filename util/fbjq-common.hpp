// util/fbjq-common.hpp
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

#if defined(DEBUG)
#include <mutex>
inline std::mutex log_output_mtx_;
#endif

namespace fbjqutil {

std::string join_argv(int argc, char* argv[]);

struct [[nodiscard]] restore_errno_t_
{
    const int save_errno_;
    restore_errno_t_() : save_errno_{ errno } { errno = 0; }
    ~restore_errno_t_() { errno = save_errno_; }
};

inline pid_t getthrid() {
    return static_cast<pid_t>(::syscall(SYS_gettid));
}

template <typename... Args>
void log_impl(const char* level, std::ostream& os, const std::source_location& loc,
    std::format_string<Args...> fmt, Args&&... args)
{
    restore_errno_t_ restore_errno_1__;

#if defined(DEBUG)
    std::lock_guard<std::mutex> lock_log_output_mtx_{ log_output_mtx_ };
#endif

    try {
        char buf[1024];

        auto result = std::format_to_n(buf, sizeof(buf),
            "{}: {}({}): {}: tid={} errno={}: ", level, loc.file_name(),loc.line(),loc.function_name(),
            getthrid(), restore_errno_1__.save_errno_);

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

constexpr const char* DEFAULT_CONFIG_FILE = "/etc/fbjq.conf";
constexpr gid_t QUEUE_FILE_GROUP = static_cast<gid_t>(0);
constexpr mode_t QUEUE_DIR_PERMISSION = static_cast<mode_t>(0500);
constexpr mode_t QUEUE_FILE_PERMISSION = static_cast<mode_t>(0400);

constexpr int VERSION_MAXLEN = 11;
constexpr int QUEUE_NAME_MAXLEN = 63;
constexpr int QUEUE_MAX_PROCESS = 32;

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
bool get_queue_item(const libconfig::Config* app_cfg, const char* q_name, queue_item_view_t* queue_item);
int for_each_queue_item(const libconfig::Config* app_cfg, const std::function<bool(const char* q_name, const queue_item_view_t& q_item)>& callback);

// util/systemd.cpp
bool systemd_unit_method(const std::string& unit_name, const char* method);

inline bool systemctl_start_unit(const std::string& unit_name) {
    return systemd_unit_method(unit_name, "StartUnit");
}

inline bool systemctl_stop_unit(const std::string& unit_name) {
    return systemd_unit_method(unit_name, "StopUnit");
}

struct request_file_header_t
{
    char magic[4];
    char version[VERSION_MAXLEN + 1];       // 16
    std::int64_t filename_ns;
    std::uint64_t filename_seq;             // 32
    std::uint32_t client_uid;
    std::uint32_t client_gid;
    std::int32_t client_pid;
    std::int32_t fuse_pid;
    std::int32_t fuse_tid;
    std::uint32_t exec_user_uid;
    std::uint32_t allow_group_gid;
    char padding1[4];                       // 64
    char q_name[QUEUE_NAME_MAXLEN + 1];     // 128
    char padding2[128];
};

static_assert(sizeof(request_file_header_t) == 256, "Header size must be 256 bytes");
static_assert(std::is_trivially_copyable_v<request_file_header_t>, "Header must be trivial");

// util/dirent.cpp
int for_each_file(const libconfig::Config* app_cfg, const std::filesystem::path& target_dir,
    const std::function<bool(const int, const std::filesystem::path&, const request_file_header_t*)>& on_regular_file);

constexpr const char* REQUEST_FILE_EXT = ".req";

} // namespace fbjqutil

template <>
struct std::formatter<std::filesystem::path> {
    constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const std::filesystem::path& p, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", p.string());
    }
};

#define LOG_ERROR(...) fbjqutil::log_impl("ERR", std::cerr, std::source_location::current(), __VA_ARGS__)

#define LOG_INFO(...) fbjqutil::log_impl("INF", std::cout, std::source_location::current(), __VA_ARGS__)

#if defined(DEBUG)
#define LOG_DEBUG(...) fbjqutil::log_impl("DBG", std::cerr, std::source_location::current(), __VA_ARGS__)
#else
#define LOG_DEBUG(...) do { } while (false)
#endif

#define NULLABLE_CSTR(x) (x) ? (x) : "(null)"

#define ENTER_FUNCTION() fbjqutil::restore_errno_t_ restore_errno_0__; LOG_DEBUG("ENTER")
