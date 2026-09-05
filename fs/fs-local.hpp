// fs/local.hpp
#pragma once

//#define _FILE_OFFSET_BITS 64
//#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <libconfig.h++>

#include "lib.hpp"

struct app_context_t
{
    const libconfig::Config* app_cfg{ nullptr };
    const std::filesystem::path& spool_dir;
    const time_t boot_time{ static_cast<time_t>(-1) };
};

const struct fuse_operations* fbjq_operations();
