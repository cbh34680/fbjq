// util/util.cpp
#include "fbjq-common.hpp"
#include <fstream>

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

int for_each_file(const libconfig::Config* app_cfg,
    const std::filesystem::path& target_dir,
    const std::function<bool(const int, const std::filesystem::path&, const char*)>& on_file)
{
    namespace fs = std::filesystem;
    ENTER_FUNCTION();

    const fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };
    LOG_DEBUG("spool_dir={}", spool_dir);

    const fs::path dead_dir{ spool_dir / "dead" };

    try {
        int regfiles = 0;

        for (const auto& entry : fs::directory_iterator(target_dir)) {
            const auto& entry_path{ entry.path() };
            LOG_DEBUG("entry path={}", entry_path);

            if (! entry.is_regular_file()) {
                const auto remove_n = fs::remove_all(entry_path);
                LOG_INFO("remove {} files", remove_n);
                continue;
            }

            fbjqutil::request_header_t header;
            const char* q_name = nullptr;

            try {
                std::ifstream ifs{ entry_path, std::ios::in | std::ios::binary };
                if (! ifs) {
                    throw std::runtime_error(std::format("{}: open error", entry_path));
                }
                // open ok

                if (! ifs.read(reinterpret_cast<char*>(&header), sizeof(header))) {
                    throw std::runtime_error(std::format("{}: read error", entry_path));
                }
                // read header ok

                if (! fbjqutil::is_valid_header(app_cfg, header)) {
                    throw std::runtime_error(std::format("{}: invalid header", entry_path));
                }

                LOG_DEBUG("ok");

                q_name = header.q_name;
            } catch (const std::exception& ex) {
                LOG_ERROR("exception: path={}: what={}", entry_path, ex.what());

            } catch (...) {
                LOG_ERROR("exception: path={}: unknown", entry_path);
            }

            if (! on_file(regfiles, entry_path, q_name)) {
                LOG_INFO("The callback rejected the continuation.");
                break;
            }

            ++regfiles;
        }

        return regfiles;

    } catch (const std::exception& ex) {
        LOG_ERROR("exception what={}", ex.what());
        return -1;

    } catch (...) {
        LOG_ERROR("unknown exception");
        return -1;
    }
}

} // namespace fbjqutil
