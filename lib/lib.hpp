// lib/lib.hpp

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
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

struct queue_item_t {
	const char* exec_user{ nullptr };
	const char* allow_group{ nullptr };
	uid_t exec_user_uid{ static_cast<uid_t>(-1) };
	gid_t allow_group_gid{ static_cast<gid_t>(-1) };
	int max_process{ 1 };
};

// util.cpp で定義される関数の宣言
uint64_t now_nanos();
std::filesystem::path get_path_from_fd(int fd);
bool get_uid_by_name(const char* user_name, uid_t* out_uid);
bool get_gid_by_name(const char* group_name, gid_t* out_gid);

std::unique_ptr<libconfig::Config> load_config(const char* cfg_file);
bool get_queue_item(const libconfig::Config* app_cfg, const char* name, queue_item_t* queue_item);
int for_each_queue_item(const libconfig::Config* app_cfg, std::function<bool(const char* q_name, const queue_item_t& q_item)> callback);

bool call_systemd_unit_method(const std::string& unit_name, const std::string& method);

// handler.cpp で定義される関数の宣言
struct app_context_t {
	const libconfig::Config* app_cfg{ nullptr };
	const std::filesystem::path& spool_dir;
	const time_t boot_time{ static_cast<time_t>(-1) };
};

struct request_header_t {
	char magic[4];
	uint32_t caller_uid;
	uint32_t caller_gid;
	int32_t caller_pid;
	int32_t fuse_pid;
	int32_t fuse_tid;
	uint32_t exec_user_uid;
	uint32_t allow_group_gid;
	char queue_name[QUEUE_NAME_MAXLEN + 1];
	unsigned char filler[28];
	char cigam[4];
};

static_assert(sizeof(request_header_t) == 96, "Header size must be 96 bytes");
static_assert(std::is_trivial<request_header_t>::value, "Header must be trivial");

}
