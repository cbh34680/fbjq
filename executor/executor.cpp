#include "local.hpp"
#include <csignal>
#include <fstream>
#include <string>

int for_each_queue_file(const libconfig::Config* app_cfg, const char* q_name,
    const std::function<bool(const std::filesystem::directory_entry&, const int)>& should_continue)
{
    namespace fs = std::filesystem;
    ENTER_FUNCTION();

    const fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };
    LOG_DEBUG("spool_dir={}", spool_dir.string());

    try {
        int moved = 0;

        for (const auto& entry : fs::directory_iterator(spool_dir / "queue" / q_name)) {
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
            LOG_DEBUG("entry path={}", entry_path.string());

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

                if (std::string_view(std::begin(header.magic), std::end(header.magic)) == "FBJQ" &&
                    std::string_view(std::begin(header.cigam), std::end(header.cigam)) == "QJBF") {
                    // go next

                } else {
                    throw std::runtime_error("invalid magic");
                }
                // check magic ok

                if (! fbjqutil::get_queue_item(app_cfg, header.q_name, nullptr)) {
                    throw std::runtime_error("get_queue_item");
                }
                // check queue ok

                LOG_DEBUG("ok");

                success = true;
                ++moved;
            } catch (const std::exception& ex) {
                LOG_ERROR("exception: path={}: what={}", entry.path().c_str(), ex.what());

            } catch (...) {
                LOG_ERROR("exception: path={}: unknown", entry.path().c_str());
            }

            const fs::path newpath = success
                ? spool_dir / "queue" / header.q_name / entry_path.filename()
                : spool_dir / "dead"  / entry_path.filename();

            LOG_INFO("move: from={} to={}", entry_path.string(), newpath.string());
            fs::rename(entry_path, newpath);
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
