// delivery/local.hpp
#pragma once
#include "fbjq-common.hpp"

int for_each_delivery_file(const libconfig::Config* app_cfg,
    const std::function<bool(const std::filesystem::directory_entry&, const int)>& should_continue);
