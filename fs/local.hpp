// fs/local.hpp
#pragma once

//#define _FILE_OFFSET_BITS 64
//#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <libconfig.h++>

#include "fbjq-common.hpp"

// fs/args.cpp
struct app_args_t
{
    int check_only{ 0 };
    const char* cfg_file{ nullptr };

    std::string string() {
        return std::format("check_only={}, cfg_file={}", check_only, cfg_file);
    }
};

bool set_app_args(struct fuse_args* args, app_args_t* app_args);

// fs/operation.cpp
struct app_context_t
{
    const libconfig::Config* app_cfg{ nullptr };
    const std::filesystem::path& spool_dir;
    const time_t boot_time{ static_cast<time_t>(-1) };
};

const struct fuse_operations* fbjq_operations();

// fs/helper.cpp
struct FuseArgsHelper
{
    struct fuse_args args;

    FuseArgsHelper(int argc, char** argv);
    ~FuseArgsHelper();
};

struct SystemdUnitHelper
{
    bool success = false;
    const libconfig::Config* app_cfg;

    SystemdUnitHelper(const libconfig::Config* app_cfg_, const std::filesystem::path& spool_dir);
    ~SystemdUnitHelper();
};
