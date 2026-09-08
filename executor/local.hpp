// executor/local.hpp
#pragma once
#include "fbjq-common.hpp"

constexpr int DEFAULT_MAX_FILES = 50;

struct app_args_t
{
    int check_only{ 0 };
    const char* cfg_file{ fbjqutil::DEFAULT_CONFIG_FILE };
    int max_files{ DEFAULT_MAX_FILES };
    const char* q_name{ nullptr };

    std::string string() {
        return std::format("check_only={}, cfg_file={}, max_files={}, q_name={}",
            check_only, cfg_file, max_files, NULLABLE_CSTR(q_name));
    }
};

bool set_app_args(int argc, char** argv, app_args_t* app_args);
