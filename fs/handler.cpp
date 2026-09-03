// fs/handler.cpp
#include "local.h"
#include <iostream>
#include <cstring>

#define APP_CTX() static_cast<fbjq_context_type*>(fuse_get_context()->private_data)


static void* fbjq_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
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

	::memset(stbuf, 0, sizeof(*stbuf));

	if (::strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		stbuf->st_atime = APP_CTX()->boot_time;
		stbuf->st_mtime = APP_CTX()->boot_time;
		stbuf->st_ctime = APP_CTX()->boot_time;

		return 0;
	}

	auto app_cfg = APP_CTX()->cfg;
	if (! app_cfg->exists("queue")) {
		return -ENOENT;
	}

	const auto& queue = app_cfg->lookup("queue");

	auto path1 = path + 1; // 先頭の '/' をスキップ
	//std::cout << path1 << std::endl;

	if (queue.isGroup() && queue.exists(path1)) {
		const auto& q_item = queue[path1];

		if (is_valid_queue_item(q_item)) {
			uid_t uid;
			get_uid_by_name(q_item["exec_user"].c_str(), &uid);
			gid_t gid;
			get_gid_by_name(q_item["allow_group"].c_str(), &gid);

			stbuf->st_mode = S_IFIFO | 0620;
			stbuf->st_nlink = 1;
			stbuf->st_uid = uid;
			stbuf->st_gid = gid;
			stbuf->st_atime = APP_CTX()->boot_time;
			stbuf->st_mtime = APP_CTX()->boot_time;
			stbuf->st_ctime = APP_CTX()->boot_time;

			return 0;
		}
	}

	return -ENOENT;
}

static int fbjq_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset,
	struct fuse_file_info* fi, enum fuse_readdir_flags flags)
{
	(void) offset;

	if (::strcmp(path, "/") != 0) {
		return -ENOENT;
	}

	filler(buf, ".",  nullptr, 0, FUSE_FILL_DIR_DEFAULTS);
	filler(buf, "..", nullptr, 0, FUSE_FILL_DIR_DEFAULTS);

	auto fn = [buf, filler](const libconfig::Setting& q_item) -> bool {
		filler(buf, q_item.getName(), nullptr, 0, FUSE_FILL_DIR_DEFAULTS);
		return true;
	};

	foreach_valid_queue_items(APP_CTX()->cfg, fn);

	return 0;
}

static const struct fuse_operations fbjq_oper =
{
	.getattr	= fbjq_getattr,
	.readdir	= fbjq_readdir,
	.init       = fbjq_init,
//	.open		= fbjq_open,
//	.read		= fbjq_read,
};

const struct fuse_operations* fbjq_operations()
{
    return &fbjq_oper;
}