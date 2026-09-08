// delivery/delivery.cpp
#include "local.hpp"
#include <fstream>
#include <string>

int for_each_delivery_file(const libconfig::Config* app_cfg,
    const std::function<bool(const std::filesystem::directory_entry&, const int)>& should_continue)
{
    namespace fs = std::filesystem;
    ENTER_FUNCTION();

    const fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };
    LOG_DEBUG("spool_dir={}", spool_dir);

    try {
        int moved = 0;

        for (const auto& entry : fs::directory_iterator(spool_dir / "delivery")) {
            if (! should_continue(entry, moved)) {
                LOG_INFO("The callback rejected the continuation.");
                break;
            }

            if (! entry.is_regular_file()) {
                const auto remove_n = fs::remove_all(entry.path());
                LOG_INFO("remove {} files", remove_n);
                continue;
            }

            const auto& entry_path{ entry.path() };
            LOG_DEBUG("entry path={}", entry_path);

            bool success = false;
            fbjqutil::request_header_t header;

            try {
                std::ifstream ifs{ entry_path, std::ios::in | std::ios::binary };
                if (! ifs) {
                    throw std::runtime_error("open error");
                }
                // open ok

                if (! ifs.read(reinterpret_cast<char*>(&header), sizeof(header))) {
                    throw std::runtime_error("read error");
                }
                // read header ok

                if (! fbjqutil::is_valid_header(app_cfg, header)) {
                    throw std::runtime_error("invalid header");
                }

                LOG_DEBUG("ok");

                success = true;
            } catch (const std::exception& ex) {
                LOG_ERROR("exception: path={}: what={}", entry.path().c_str(), ex.what());

            } catch (...) {
                LOG_ERROR("exception: path={}: unknown", entry.path().c_str());
            }

            const fs::path newpath = success
                ? spool_dir / "queue" / header.q_name / entry_path.filename()
                : spool_dir / "dead"  / entry_path.filename();

            LOG_INFO("move: from={} to={}", entry_path, newpath);
            fs::rename(entry_path, newpath);

            ++moved;
        }

        return moved;

    } catch (const std::exception& ex) {
        LOG_ERROR("exception what={}", ex.what());
        return -1;

    } catch (...) {
        LOG_ERROR("unknown exception");
        return -1;
    }
}
