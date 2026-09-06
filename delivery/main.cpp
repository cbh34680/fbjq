// delivery/main.cpp
#include "lib.hpp"
#include <csignal>
#include <atomic>
#include <iostream>
#include <fstream>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>

static void signal_handler(int signum);
static int for_each_delivery_file(const libconfig::Config* app_cfg);
static std::atomic<bool> g_graceful_stop{ false };


int main(int argc, char** argv)
{
    ENTER_FUNCTION();

    ::umask(0);

    int opt = 0;
    int option_index = 0;

    // ロングオプションの定義
    static struct option long_options[] = {
        {"check",   no_argument,       nullptr, 'C'},
        {"config",  required_argument, nullptr, 'c'},
        {nullptr,   0,                 nullptr, 0  }
    };

    // オプション指定なし（または引数不足）のエラーメッセージを無効化する場合は 0 に設定
    // opterr = 0;

    struct app_args_t
    {
        int check_only{ 0 };
        const char* cfg_file{ fbjqlib::DEFAULT_CONFIG_FILE };
    };

    app_args_t app_args;

    while ((opt = getopt_long(argc, argv, "Cc:", long_options, &option_index)) != -1) {
        switch (opt)
        {
            case 'C':
                app_args.check_only = 1;
                break;

            case 'c':
                app_args.cfg_file = optarg;
                break;

            case '?':
                // 未知のオプション、または引数が不足している場合
                LOG_ERROR("Unknown option or missing argument.");
                return 1;

            default:
                break;
        }
    }

    // 設定ファイルの読み込み
    auto appConfigPtr{ fbjqlib::load_config(app_args.cfg_file) };
    if (appConfigPtr) {
        if (app_args.check_only) {
            LOG_INFO("config check ok");
            return EXIT_SUCCESS;
        }
    } else {
        LOG_ERROR("load_config");
        return EXIT_FAILURE;
    }

    LOG_INFO("load_config config={}", app_args.cfg_file);

    //
    struct sigaction sa{};

    // ハンドラ関数の登録
    sa.sa_handler = signal_handler;
    
    // シグナル処理中に他のシグナルをブロックするためのマスクをクリア
    sigemptyset(&sa.sa_mask);

    // SA_RESTART: シグナル割り込みによって中断されたシステムコールを自動再開する
    sa.sa_flags = SA_RESTART;

    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGINT, &sa, nullptr);

    //
    int result = for_each_delivery_file(appConfigPtr.get());
    if (result < 0) {
        LOG_ERROR("for_each_delivery_file");
        return EXIT_FAILURE;
    }

    LOG_INFO("Validated {} files, g_graceful_stop={}", result, static_cast<bool>(g_graceful_stop));

    return EXIT_SUCCESS;
}

static void signal_handler(int signum)
{
    if (signum == SIGTERM || signum == SIGINT) {
        g_graceful_stop = true; // ループを抜けるフラグを立てる
    }
}

static int for_each_delivery_file(const libconfig::Config* app_cfg)
{
    namespace fs = std::filesystem;
    ENTER_FUNCTION();

    const fs::path spool_dir{ app_cfg->lookup("spool_dir").c_str() };
    LOG_DEBUG("spool_dir={}", spool_dir.string());

    const auto dead_dir{ spool_dir / "dead" };

    try {
        int i = 0;
        
        for (const auto& entry : fs::directory_iterator(spool_dir / "delivery")) {
            if (g_graceful_stop) {
                LOG_INFO("receive signal, graceful stop");
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
            fbjqlib::request_header_t header;

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

                if (std::string_view(std::begin(header.magic), std::end(header.magic)) != "FBJQ") {
                    throw std::runtime_error("invalid magic");
                }

                if (std::string_view(std::begin(header.cigam), std::end(header.cigam)) != "QJBF") {
                    throw std::runtime_error("invalid magic");
                }
                // check magic ok

                if (! fbjqlib::get_queue_item(app_cfg, header.q_name, nullptr)) {
                    throw std::runtime_error("get_queue_item");
                }
                // check queue ok

                LOG_DEBUG("ok");

                success = true;
                ++i;
            } catch (const std::exception& ex) {
                LOG_ERROR("exception: path={}: what={}", entry.path().c_str(), ex.what());

            } catch (...) {
                LOG_ERROR("exception: path={}: unknown", entry.path().c_str());
            }

            const fs::path newpath = success
                ? spool_dir / "queue" / header.q_name / entry_path.filename()
                : dead_dir / entry_path.filename();

            LOG_INFO("move: from={} to={}", entry_path.string(), newpath.string());
            fs::rename(entry_path, newpath);
        }

        return i;

    } catch (const std::exception& ex) {
        LOG_ERROR("exception what={}", ex.what());
        return -1;

    } catch (...) {
        LOG_ERROR("unknown exception");
        return -1;
    }
}
