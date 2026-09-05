// lib/lib.hpp
#pragma once

#include <cerrno>
#include <cstdint>
#include <iostream>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <source_location>
#include <string>
#include <sys/types.h>
#include <libconfig.h++>
#include <grp.h>
#include <pwd.h>

namespace fbjqlib {

inline constexpr const char* DEFAULT_CONFIG_FILE = "/etc/fbjq.conf";
inline constexpr gid_t DEFAULT_FILE_GROUP = static_cast<gid_t>(0);
inline constexpr int QUEUE_MAX_PROCESS = 32;
inline constexpr int QUEUE_NAME_MAXLEN = 31;

struct queue_item_view_t {
    // exec_user / allow_group は Setting の内部バッファを指す生ポインタ。
    // 取得元の Config が生きている短いスコープでのみ使用すること。
    const char* exec_user{ nullptr };
    const char* allow_group{ nullptr };
    uid_t exec_user_uid{ static_cast<uid_t>(-1) };
    gid_t allow_group_gid{ static_cast<gid_t>(-1) };
    int max_process{ 1 };
};

// util.cpp で定義される関数の宣言
std::int64_t now_nanos();
bool get_path_from_fd(int fd, char* buf, size_t buf_siz);
bool get_uid_by_name(const char* user_name, uid_t* out_uid);
bool get_gid_by_name(const char* group_name, gid_t* out_gid);

std::unique_ptr<libconfig::Config> load_config(const char* cfg_file);
bool get_queue_item(const libconfig::Config* app_cfg, const char* name, queue_item_view_t* queue_item);
int for_each_queue_item(const libconfig::Config* app_cfg, std::function<bool(const char* q_name, const queue_item_view_t& q_item)> callback);

bool call_systemd_unit_method(const std::string& unit_name, const std::string& method);

// handler.cpp で定義される関数の宣言
struct app_context_t {
    const libconfig::Config* app_cfg{ nullptr };
    const std::filesystem::path& spool_dir;
    const time_t boot_time{ static_cast<time_t>(-1) };
};

struct request_header_t {
    char magic[4];
    char version[4];
    uint32_t client_uid;
    uint32_t client_gid;
    int32_t client_pid;
    int32_t fuse_pid;
    int32_t fuse_tid;
    uint32_t exec_user_uid;
    uint32_t allow_group_gid;
    unsigned char padding1[4];
    char queue_name[QUEUE_NAME_MAXLEN + 1];
    unsigned char padding2[52];
    char cigam[4];
};

static_assert(sizeof(request_header_t) == 128, "Header size must be 128 bytes");
static_assert(std::is_trivially_copyable_v<request_header_t>, "Header must be trivial");

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
    restore_errno_t_ restore_errno__;

    try {
        char buf[512];

        auto result = std::format_to_n(buf, sizeof(buf),
            "{}: {}({}): {}: e={}: ", level, loc.file_name(),loc.line(),loc.function_name(), restore_errno__.save_errno_);

        const auto prefix_size = static_cast<std::size_t>(result.out - buf);

        if (prefix_size >= sizeof(buf)) {
            os.write(buf, sizeof(buf));
            os.put('\n');
            return;
        }

        result = std::format_to_n(buf + prefix_size,
            sizeof(buf) - prefix_size, fmt, std::forward<Args>(args)...);

        const auto total_size = static_cast<std::size_t>(result.out - buf);

        os.write(buf, total_size);
        os.put('\n');
    }
    catch (...) {
        // ignore
    }
}

} // namespace

#define LOG_ERROR(...) fbjqlib::log_impl("ERR", std::cerr, std::source_location::current(), __VA_ARGS__)

#define LOG_INFO(...) fbjqlib::log_impl("INF", std::cout, std::source_location::current(), __VA_ARGS__)

#if defined(DEBUG)
#define LOG_DEBUG(...) fbjqlib::log_impl("DBG", std::cerr, std::source_location::current(), __VA_ARGS__)
#else
#define LOG_DEBUG(...) do { } while (false)
#endif

#define ENTER_FUNCTION() fbjqlib::restore_errno_t_ restore_errno__; LOG_DEBUG("ENTER")

inline pid_t gettid() {
    return static_cast<pid_t>(::syscall(SYS_gettid));
}

