// util/util.cpp
#include "fbjq-common.hpp"

namespace fbjqutil {

// ナノ秒精度の Epoch タイムスタンプ
std::int64_t now_nanos()
{
    ENTER_FUNCTION();

    struct std::timespec ts;
    ::clock_gettime(CLOCK_REALTIME, &ts);

    // ナノ秒（Epochからの通算ナノ秒）
    return static_cast<std::int64_t>(ts.tv_sec) * std::int64_t{ 1000000000 } + ts.tv_nsec;
}

bool get_path_from_fd(int fd, char* buf, size_t buf_siz)
{
    ENTER_FUNCTION();

    if (buf == nullptr || buf_siz == 0) {
        LOG_ERROR("invalid params");
        return false;
    }

    char proc_path[PATH_MAX];
    std::snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", fd);

    const auto len = ::readlink(proc_path, buf, buf_siz - 1);
    if (len == -1) {
        LOG_ERROR("{}: readlink", proc_path);
        return false;
    }

    buf[len] = '\0';

    return true;
}

// ユーザー名から UID を取得する関数 (成功時: true, 失敗時: false)
bool get_uid_by_name(const char* user_name, uid_t* out_uid)
{
    ENTER_FUNCTION();

    struct passwd pwd;
    struct passwd* result = nullptr;

    char buf[1024];
    int res = ::getpwnam_r(user_name, &pwd, buf, sizeof(buf), &result);

    if (res == 0 && result != nullptr) {
        if (out_uid) {
            *out_uid = result->pw_uid;
        }

        return true;
    }

    LOG_ERROR("getpwnam_r");

    return false;
}

// グループ名から GID を取得する関数 (成功時: true, 失敗時: false)
bool get_gid_by_name(const char* group_name, gid_t* out_gid)
{
    ENTER_FUNCTION();

    struct group grp;
    struct group* result = nullptr;

    char buf[1024];
    int res = ::getgrnam_r(group_name, &grp, buf, sizeof(buf), &result);

    if (res == 0 && result != nullptr) {
        if (out_gid) {
            *out_gid = result->gr_gid;
        }

        return true;
    }

    LOG_ERROR("getgrnam_r");

    return false;
}

} // namespace fbjqutil
