// fs/operation.cpp
#include "local.hpp"
#include <cinttypes>
#include <cstring>
#include <atomic>

#define APP_CTX() static_cast<app_context_t*>(fuse_get_context()->private_data)

namespace {

void* fbjq_init(struct fuse_conn_info* conn, struct fuse_config* cfg)
{
    ENTER_FUNCTION();

#ifdef FUSE_CAP_PASSTHROUGH
    // カーネル側が Passthrough に対応していれば有効化を要求
    if (conn->capable & FUSE_CAP_PASSTHROUGH) {
        conn->want |= FUSE_CAP_PASSTHROUGH;
    }
#endif

#if defined(FUSE_CONN_FLAG_SINGLE_ISSUER)
    /* Always replies inline on the io-uring worker thread */
    fuse_set_conn_flag(conn, FUSE_CONN_FLAG_SINGLE_ISSUER);
#endif

    cfg->kernel_cache = 0;

    /* Test setting flags the old way */
    //fuse_set_feature_flag(conn, FUSE_CAP_ASYNC_READ);
    //fuse_unset_feature_flag(conn, FUSE_CAP_ASYNC_READ);

    //APP_CTX()->cfg->write(stdout);

    // Return the context pointer to be used in other callbacks

    return APP_CTX();
}

int fbjq_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
{
    (void) fi;
    ENTER_FUNCTION();

    std::memset(stbuf, 0, sizeof(*stbuf));

    if (std::strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_atime = APP_CTX()->boot_time;
        stbuf->st_mtime = APP_CTX()->boot_time;
        stbuf->st_ctime = APP_CTX()->boot_time;

        return 0;
    }

    auto app_cfg = APP_CTX()->app_cfg;

    const char* q_name = path + 1;
    fbjqutil::queue_item_view_t q_item;

    if (! fbjqutil::get_queue_item(app_cfg, q_name, &q_item)) {
        LOG_ERROR("get_queue_item");
        return -ENOENT;
    }

    stbuf->st_mode = S_IFREG | 0220;
    stbuf->st_nlink = 1;
    stbuf->st_uid = q_item.exec_user_uid;
    stbuf->st_gid = q_item.allow_group_gid;
    stbuf->st_atime = APP_CTX()->boot_time;
    stbuf->st_mtime = APP_CTX()->boot_time;
    stbuf->st_ctime = APP_CTX()->boot_time;

    return 0;
}

int fbjq_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset,
    struct fuse_file_info* fi, enum fuse_readdir_flags flags)
{
    (void) offset;
    (void) fi;
    (void) flags;
    ENTER_FUNCTION();

    if (std::strcmp(path, "/") != 0) {
        LOG_ERROR("{}: path != /", path);
        return -ENOENT;
    }

    filler(buf, ".",  nullptr, 0, FUSE_FILL_DIR_DEFAULTS);
    filler(buf, "..", nullptr, 0, FUSE_FILL_DIR_DEFAULTS);

    const auto append_queue = [&](const char* q_name, const auto& q_item) -> bool {
        (void) q_item;

        filler(buf, q_name, nullptr, 0, FUSE_FILL_DIR_DEFAULTS);
        return true;
    };

    fbjqutil::for_each_queue_item(APP_CTX()->app_cfg, append_queue);

    return 0;
}

int fbjq_open(const char *path, struct fuse_file_info *fi)
{
    ENTER_FUNCTION();

    const int acc_mode = fi->flags & O_ACCMODE;

    if (acc_mode == O_WRONLY || acc_mode == O_RDWR) {
        // go next
    } else {
        LOG_ERROR("{}: illegal acc_mode", acc_mode);
        return -EPERM;
    }

    if (fi->flags & O_EXCL) {
        LOG_ERROR("{}: illegal fi->flags", fi->flags);
        return -EPERM;
    }

    const struct fuse_context* fuse_ctx = fuse_get_context();

    const char* q_name = path + 1;
    fbjqutil::queue_item_view_t q_item;

    if (! fbjqutil::get_queue_item(APP_CTX()->app_cfg, q_name, &q_item)) {
        LOG_ERROR("get_queue_item");
        return -ENOENT;
    }

    const auto now = fbjqutil::now_nanos();
    static std::atomic_uint64_t sequence{ 0 };
    const auto seq = sequence.fetch_add(1, std::memory_order_relaxed);

    char outpath[PATH_MAX];
    std::snprintf(outpath, sizeof(outpath), "%s/tmp/%" PRId64 "-%" PRIu64 ".dat", APP_CTX()->spool_dir.c_str(), now, seq);

    fbjqutil::request_header_t header
    {
        .magic              = { 'F', 'B', 'J', 'Q' },
        .version            = { '0', '0', '1', '0' },
        .client_uid         = static_cast<uint32_t>(fuse_ctx->uid),
        .client_gid         = static_cast<uint32_t>(fuse_ctx->gid),
        .client_pid         = static_cast<int32_t>(fuse_ctx->pid),
        .fuse_pid           = static_cast<int32_t>(::getpid()),
        .fuse_tid           = static_cast<int32_t>(fbjqutil::getthrid()),
        .exec_user_uid      = static_cast<uint32_t>(q_item.exec_user_uid),
        .allow_group_gid    = static_cast<uint32_t>(q_item.allow_group_gid),
        .padding1           = { '\0' },
        .q_name             = { '\0' },
        .padding2           = { '\0' },
        .cigam              = { 'Q', 'J', 'B', 'F' },
    };

    ::strncpy(header.q_name, q_name, sizeof(header.q_name));

    // write header
    int rc = 0;
    ssize_t written = -1;

    int fh = ::open(outpath, O_WRONLY | O_CREAT | O_EXCL, 0400);
    if (fh == -1) {
        rc = -errno;
        LOG_ERROR("{}: open", outpath);
        goto EXIT_LABEL;
    }

    if (::fchown(fh, q_item.exec_user_uid, fbjqutil::DEFAULT_FILE_GROUP) != 0) {
        rc = -errno;
        LOG_ERROR("chown");
        goto EXIT_LABEL;
    }

    written = TEMP_FAILURE_RETRY(::write(fh, &header, sizeof(header)));
    if (written == -1) {
        rc = -errno;
        LOG_ERROR("write");
        goto EXIT_LABEL;
    }

    if (written != sizeof(header)) {
        rc = -EIO;
        LOG_ERROR("written={} <> size={}", written, sizeof(header));
        goto EXIT_LABEL;
    }

    fi->fh = fh;
    fh = -1;

EXIT_LABEL:
    if (fh != -1) {
        ::close(fh);
    }

    return rc;
}

int fbjq_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    (void) path;
    ENTER_FUNCTION();

    const int fd = static_cast<int>(fi->fh);
    if (fd < 0) {
        LOG_ERROR("{}: illegal fd", fd);
        return -EBADF;
    }

    ssize_t written = TEMP_FAILURE_RETRY(::pwrite(fd, buf, size, offset + sizeof(fbjqutil::request_header_t)));
    if (written == -1) {
        LOG_ERROR("pwrite");
        return -errno;
    }

    if (written > INT_MAX) {
        LOG_ERROR("pwrite returned too large value: {}", written);
        return -EOVERFLOW;
    }

    return static_cast<int>(written);
}

int fbjq_release(const char* path, struct fuse_file_info* fi)
{
    (void) path;
    ENTER_FUNCTION();

    const int fd = static_cast<int>(fi->fh);
    if (fd < 0) {
        LOG_ERROR("{}: illegal fd", fd);
        return -EBADF;
    }

    int rc = 0;
    char oldpath[PATH_MAX];
    char newpath[PATH_MAX];

    if (! fbjqutil::get_path_from_fd(fd, oldpath, sizeof(oldpath))) {
        rc = -EBADF;
        LOG_ERROR("get_path_from_fd");
        goto EXIT_LABEL;
    }

    {
        char copy_oldpath[PATH_MAX];
        std::strcpy(copy_oldpath, oldpath);

        const char* oldfile = ::basename(copy_oldpath);
        std::snprintf(newpath, sizeof(newpath), "%s/delivery/%s", APP_CTX()->spool_dir.c_str(), oldfile);
    }

    if (::rename(oldpath, newpath) == -1) {
        rc = -errno;
        LOG_ERROR("rename to={}", newpath);
        goto EXIT_LABEL;
    }

    LOG_INFO("regist path={}", newpath);

EXIT_LABEL:
    if (::close(fd) == -1) {
        LOG_ERROR("close");
    }

    return rc;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
const struct fuse_operations fbjq_oper = {
    .getattr    = fbjq_getattr,
    .open       = fbjq_open,
    .write      = fbjq_write,
    .release    = fbjq_release,
    .readdir    = fbjq_readdir,
    .init       = fbjq_init,
};
#pragma GCC diagnostic pop

} // namespace

const struct fuse_operations* fbjq_operations()
{
    ENTER_FUNCTION();
    return &fbjq_oper;
}