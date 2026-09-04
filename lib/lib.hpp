// lib/lib.hpp

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <sys/types.h>
#include <libconfig.h++>

namespace fbjqlib {

inline constexpr const char* DEFAULT_CONFIG_FILE = "/etc/fbjq.conf";

// util.cpp で定義される関数の宣言
bool get_uid_by_name(const char* user_name, uid_t* out_uid);
bool get_gid_by_name(const char* group_name, gid_t* out_gid);
std::unique_ptr<libconfig::Config> load_config(const char* config_file);
bool is_valid_queue_item(const libconfig::Setting& q_item);
int for_each_queue_item(const libconfig::Config* cfg, std::function<bool(const libconfig::Setting&)> fn);
bool call_systemd_unit_method(const std::string& unit_name, const std::string& method);

// handler.cpp で定義される関数の宣言
struct context_type {
	const libconfig::Config* cfg;
	//const std::filesystem::path mountpoint;
	const std::filesystem::path spool_dir;
	const time_t boot_time;
};

}
