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
inline constexpr int QUEUE_MAX_PROCESS = 32;

struct queue_item {
	const char* exec_user{ nullptr };
	const char* allow_group{ nullptr };
	uid_t exec_user_uid{ static_cast<uid_t>(-1) };
	gid_t allow_group_gid{ static_cast<gid_t>(-1) };
	int max_process{ 1 };
};

// util.cpp で定義される関数の宣言
bool get_uid_by_name(const char* user_name, uid_t* out_uid);
bool get_gid_by_name(const char* group_name, gid_t* out_gid);

std::unique_ptr<libconfig::Config> load_config(const char* cfg_file);
bool get_queue_item(const libconfig::Config* app_cfg, const char* name, queue_item* queue_item);
int for_each_queue_item(const libconfig::Config* app_cfg, std::function<bool(const char* q_name, const queue_item& q_item)> callback);

bool call_systemd_unit_method(const std::string& unit_name, const std::string& method);

// handler.cpp で定義される関数の宣言
struct context_type {
	const libconfig::Config* app_cfg{ nullptr };
	//const std::filesystem::path mountpoint;
	const std::filesystem::path& spool_dir;
	const time_t boot_time{ static_cast<time_t>(-1) };
};

}
