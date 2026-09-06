// fs/local.hpp
#pragma once

//#define _FILE_OFFSET_BITS 64
//#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <libconfig.h++>

#include "fbjq-util.hpp"

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
