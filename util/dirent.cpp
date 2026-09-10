// util/dirent.cpp
#include "fbjq-common.hpp"
#include <cstring>
#include <fstream>

namespace fbjqutil {

int for_each_file(const libconfig::Config* app_cfg, const std::filesystem::path& target_dir, const int max_files,
    const std::function<bool(const std::filesystem::path&, const request_file_header_t*)>& on_regular_file)
{
    namespace fs = std::filesystem;
    ENTER_FUNCTION();

    const char* cfg_version{ app_cfg->lookup("version").c_str() };
    LOG_DEBUG("cfg_version={}", cfg_version);

    const fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };
    LOG_DEBUG("spool_dir={}", spool_dir);

    const fs::path dead_dir{ spool_dir / "dead" };

    try {
        int regfiles = 0;

        for (const auto& entry : fs::directory_iterator(target_dir)) {
            const auto& entry_path{ entry.path() };
            LOG_DEBUG("entry path={}", entry_path);

            if (! entry.is_regular_file()) {
                const auto remove_n = fs::remove_all(entry_path);
                LOG_INFO("remove {} files", remove_n);
                continue;
            }

            request_file_header_t rfhdr_;
            request_file_header_t* rfhdr = nullptr;

            try {
                std::ifstream ifs{ entry_path, std::ios::in | std::ios::binary };
                if (! ifs) {
                    throw std::runtime_error(std::format("{}: open error", entry_path));
                }
                // open ok

                if (! ifs.read(reinterpret_cast<char*>(&rfhdr_), sizeof(rfhdr_))) {
                    throw std::runtime_error(std::format("{}: read error", entry_path));
                }
                // read header ok

                if (std::string_view(std::begin(rfhdr_.magic), std::end(rfhdr_.magic)) == "FBJQ") {
                    // go next

                } else {
                    throw std::runtime_error(std::format("{}: invalid magic", entry_path));
                }
                // check magic ok

                if (::strcmp(rfhdr_.version, cfg_version) != 0) {
                    throw std::runtime_error(std::format("{}: invalid version", entry_path));
                }
                // check version ok

                if (! get_queue_item(app_cfg, rfhdr_.q_name, nullptr)) {
                    throw std::runtime_error(std::format("{}: get_queue_item", entry_path));
                }
                // check queue-item ok

                LOG_DEBUG("ok");

                rfhdr = &rfhdr_;
            } catch (const std::exception& ex) {
                LOG_ERROR("exception: path={}: what={}", entry_path, ex.what());

            } catch (...) {
                LOG_ERROR("exception: path={}: unknown", entry_path);
            }

            if (! on_regular_file(entry_path, rfhdr)) {
                LOG_INFO("The callback rejected the continuation.");
                break;
            }

            ++regfiles;

            if (max_files > 0) {
                if (regfiles >= max_files) {
                    // .path の停止を検知するために一定数を処理したら .service を終了する
                    LOG_INFO("The maximum number of processes has been reached.");
                    break;
                }
            }
        }

        return regfiles;

    } catch (const std::exception& ex) {
        LOG_ERROR("exception what={}", ex.what());
        return -1;

    } catch (...) {
        LOG_ERROR("unknown exception");
        return -1;
    }
}

} // fbjqutil
