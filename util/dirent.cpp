// util/dirent.cpp
#include <cstring>
#include <fstream>
#include "fbjq-common.hpp"

namespace fbjqutil {

int for_each_file(const libconfig::Config* app_cfg, const std::filesystem::path& target_dir,
    const int max_files, const for_each_file_callback_t& on_regular_file)
{
    namespace fs = std::filesystem;
    ENTER_FUNCTION();

    const char* cfg_version{ app_cfg->lookup("version").c_str() };
    LOG_DEBUG("cfg_version={}", cfg_version);

    const fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };
    LOG_DEBUG("spool_dir={}", spool_dir);

    try {
        std::vector<std::filesystem::path> regfiles;
        if (max_files > 0) {
            regfiles.reserve(max_files);
        }

        for (const auto& entry : fs::directory_iterator(target_dir)) {
            const auto& entry_path{ entry.path() };
            LOG_DEBUG("entry path={}", entry_path);

            if (! entry.is_regular_file()) {
                const auto remove_n = fs::remove_all(entry_path);
                LOG_INFO("remove {} files", remove_n);

                continue;
            }

            regfiles.push_back(entry_path);

            if (max_files > 0) {
                if (regfiles.size() >= static_cast<size_t>(max_files)) {
                    // .path の停止を検知するために一定数を処理したら .service を終了する
                    LOG_INFO("The maximum number of processes has been reached.");
                    break;
                }
            }
        }

        std::sort(regfiles.begin(), regfiles.end());

        int fret = 0;

        for (const auto& entry_path: regfiles) {
            bool ok = false;

            try {
                std::ifstream ifs{ entry_path, std::ios::in | std::ios::binary };
                if (! ifs) {
                    throw std::runtime_error(std::format("{}: open error", entry_path));
                }

                request_file_header_t rfhdr;
                if (! ifs.read(reinterpret_cast<char*>(&rfhdr), sizeof(rfhdr))) {
                    throw std::runtime_error(std::format("{}: read error", entry_path));
                }

                if (std::string_view(std::begin(rfhdr.magic), std::end(rfhdr.magic)) != "FBJQ") {
                    throw std::runtime_error(std::format("{}: invalid magic", entry_path));
                }

                if (::strcmp(rfhdr.version, cfg_version) != 0) {
                    throw std::runtime_error(std::format("{}: unknown version", entry_path));
                }

                if (! get_queue_item(app_cfg, rfhdr.q_name, nullptr)) {
                    throw std::runtime_error(std::format("{}: get_queue_item", entry_path));
                }

                const auto result = on_regular_file(entry_path, &rfhdr);

                if (result == fbjqutil::OnRegularFileResult::Error) {
                    LOG_INFO("{}: Callback processing failed.", entry_path);
                    return -1;

                } else if (result == fbjqutil::OnRegularFileResult::Break) {
                    LOG_INFO("{}: The callback refused the continuation.", entry_path);
                    break;
                }

                ok = true;

            } catch (const std::exception& ex) {
                LOG_ERROR("catch exception what={}", ex.what());

            } catch (...) {
                LOG_ERROR("catch exception unknown");
            }

            if (ok) {
                ++fret;

            } else {
                const auto newpath{ spool_dir / "dead" / entry_path.filename() };
                LOG_INFO("move to newpath={}", newpath);
                fs::rename(entry_path, newpath);
            }
        } // for

        return fret;

    } catch (const std::exception& ex) {
        LOG_ERROR("exception what={}", ex.what());
        return -1;

    } catch (...) {
        LOG_ERROR("unknown exception");
        return -1;
    }
}

} // fbjqutil
