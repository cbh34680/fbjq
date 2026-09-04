// fs/local.hpp
#pragma once

//#define _FILE_OFFSET_BITS 64
//#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <libconfig.h++>

#include "lib.hpp"

const struct fuse_operations* fbjq_operations();
