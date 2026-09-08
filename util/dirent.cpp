// util/dirent.cpp
#include "fbjq-common.hpp"
#include <fstream>

namespace fbjqutil {

int for_each_file(const libconfig::Config* app_cfg,
    const std::filesystem::path& target_dir,
    const std::function<bool(const int, const std::filesystem::path&, const char*)>& on_file)
{
    namespace fs = std::filesystem;
    ENTER_FUNCTION();

    const std::string cfg_version{ app_cfg->lookup("version").c_str() };
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

            request_header_t header;
            const char* q_name = nullptr;

            try {
                std::ifstream ifs{ entry_path, std::ios::in | std::ios::binary };
                if (! ifs) {
                    throw std::runtime_error(std::format("{}: open error", entry_path));
                }
                // open ok

                if (! ifs.read(reinterpret_cast<char*>(&header), sizeof(header))) {
                    throw std::runtime_error(std::format("{}: read error", entry_path));
                }
                // read header ok

                if (std::string_view(std::begin(header.magic), std::end(header.magic)) == "FBJQ" &&
                    std::string_view(std::begin(header.cigam), std::end(header.cigam)) == "QJBF") {
                    // go next

                } else {
                    throw std::runtime_error(std::format("{}: invalid magic", entry_path));
                }
                // check magic ok

                if (std::string_view(std::begin(header.version), std::end(header.version)) != cfg_version) {
                    throw std::runtime_error(std::format("{}: invalid version", entry_path));
                }
                // check version ok

                if (! get_queue_item(app_cfg, header.q_name, nullptr)) {
                    throw std::runtime_error(std::format("{}: get_queue_item", entry_path));
                }
                // check queue-item ok

                LOG_DEBUG("ok");

                q_name = header.q_name;
            } catch (const std::exception& ex) {
                LOG_ERROR("exception: path={}: what={}", entry_path, ex.what());

            } catch (...) {
                LOG_ERROR("exception: path={}: unknown", entry_path);
            }

            if (! on_file(regfiles, entry_path, q_name)) {
                LOG_INFO("The callback rejected the continuation.");
                break;
            }

            ++regfiles;
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
