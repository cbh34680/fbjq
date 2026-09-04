// fs/operation.cpp

#include "local.hpp"
#include <iostream>
#include <cstring>
#include <sys/syscall.h>

#define APP_CTX() static_cast<fbjqlib::app_context_t*>(fuse_get_context()->private_data)

static void* fbjq_init(struct fuse_conn_info* conn, struct fuse_config* cfg)
{
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

static int fbjq_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
{
    (void) fi;

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
	if (! app_cfg->exists("queue")) {
		return -ENOENT;
	}

	fbjqlib::queue_item_t q_item;
	if (! fbjqlib::get_queue_item(app_cfg, path + 1, &q_item)) {
		return -ENOENT;
	}

	stbuf->st_mode = S_IFREG | 0620;
	stbuf->st_nlink = 1;
	stbuf->st_uid = q_item.exec_user_uid;
	stbuf->st_gid = q_item.allow_group_gid;
	stbuf->st_atime = APP_CTX()->boot_time;
	stbuf->st_mtime = APP_CTX()->boot_time;
	stbuf->st_ctime = APP_CTX()->boot_time;

	return 0;
}

static int fbjq_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset,
	struct fuse_file_info* fi, enum fuse_readdir_flags flags)
{
	(void) offset;
	(void) fi;
	(void) flags;

	if (std::strcmp(path, "/") != 0) {
		return -ENOENT;
	}

	filler(buf, ".",  nullptr, 0, FUSE_FILL_DIR_DEFAULTS);
	filler(buf, "..", nullptr, 0, FUSE_FILL_DIR_DEFAULTS);

	const auto append_queue = [buf, filler](const char* q_name, const auto& q_item) -> bool {
		(void) q_item;
		
		filler(buf, q_name, nullptr, 0, FUSE_FILL_DIR_DEFAULTS);
		return true;
	};

	fbjqlib::for_each_queue_item(APP_CTX()->app_cfg, append_queue);

	return 0;
}

static int fbjq_open(const char *path, struct fuse_file_info *fi)
{
	const int acc_mode = fi->flags & O_ACCMODE;

	if (acc_mode == O_WRONLY || acc_mode == O_RDWR) {
		// go next
	} else {
		return -EPERM;
	}

	if (fi->flags & O_EXCL) {
		return -EPERM;
	}

	const struct fuse_context* fuse_ctx = fuse_get_context();

	fbjqlib::queue_item_t q_item;

	if (! fbjqlib::get_queue_item(APP_CTX()->app_cfg, path + 1, &q_item)) {
		return -ENOENT;
	}

	const auto now = fbjqlib::now_nanos();
	const auto pid = ::getpid();
	const auto tid = (pid_t)::syscall(SYS_gettid);
	const auto filename = std::to_string(now) + "-" + std::to_string(fuse_ctx->pid) + "-" + std::to_string(pid) + "-" + std::to_string(tid) + ".dat";

	const auto outpath{ APP_CTX()->spool_dir / "tmp" / filename };

	const int fh = ::open(outpath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0400);
	if (fh == -1) {
		return -errno;
	}

	if (::fchown(fh, q_item.exec_user_uid, fbjqlib::DEFAULT_FILE_GROUP) != 0) {
		return -errno;
	}

	const fbjqlib::request_header_t header{
		.magic				= { 'F', 'B', 'J', 'Q', },
		.caller_uid			= static_cast<uint32_t>(fuse_ctx->uid),
		.caller_gid			= static_cast<uint32_t>(fuse_ctx->gid),
		.caller_pid			= static_cast<int32_t>(fuse_ctx->pid),
		.fuse_pid			= static_cast<int32_t>(pid),
		.fuse_tid			= static_cast<int32_t>(tid),
		.exec_user_uid		= static_cast<uint32_t>(q_item.exec_user_uid),
		.allow_group_gid	= static_cast<uint32_t>(q_item.allow_group_gid),
	};

	const ssize_t written = TEMP_FAILURE_RETRY(::write(fh, &header, sizeof(header)));

	if (written == -1) {
		return -errno;
	}

	if (written != sizeof(header)) {
		return -EIO;
	}

	fi->fh = fh;

	return 0;
}

static int fbjq_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void) path;

	const int fd = static_cast<int>(fi->fh);
	if (fd <= 0) {
		return -EBADF;
	}

	ssize_t written = TEMP_FAILURE_RETRY(::pwrite(fd, buf, size, offset + sizeof(fbjqlib::request_header_t)));
	if (written == -1) {
		return -errno;
	}

	return static_cast<int>(written);
}

static int fbjq_release(const char* path, struct fuse_file_info* fi)
{
	(void) path;

	const int fd = static_cast<int>(fi->fh);
	if (fd <= 0) {
		return -EBADF;
	}

	const auto oldpath{ fbjqlib::get_path_from_fd(fd) };

	if (::close(fd) == -1) {
		return -errno;
	}

	if (oldpath.empty()) {
		return -ENOENT;
	}

	const auto newpath{ APP_CTX()->spool_dir / "delivery" / oldpath.filename() };

	if (::rename(oldpath.c_str(), newpath.c_str()) == -1) {
		return -errno;
	}

	std::cout << "move from=" << oldpath << " to=" << newpath << std::endl;

	return 0;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const struct fuse_operations fbjq_oper =
{
	.getattr	= fbjq_getattr,
	.open		= fbjq_open,
	.write		= fbjq_write,
	.release	= fbjq_release,
	.readdir	= fbjq_readdir,
	.init       = fbjq_init,
};
#pragma GCC diagnostic pop

const struct fuse_operations* fbjq_operations()
{
    return &fbjq_oper;
}