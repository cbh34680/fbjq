// delivery/local.hpp
#pragma once
#include "fbjq-common.hpp"

struct app_args_t
{
    int check_only = 0;
    const char* cfg_file = fbjqutil::DEFAULT_CONFIG_FILE;
    int max_files = 0;

    std::string string() {
        return std::format("check_only={}, cfg_file={}, max_files={}", check_only, cfg_file, max_files);
    }
};

bool set_app_args(int argc, char** argv, app_args_t* app_args);
