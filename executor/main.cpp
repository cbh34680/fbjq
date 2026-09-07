// executor/main.cpp
#include "local.hpp"
#include <csignal>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>

/*
[fbjq-executor プログラム仕様]
1) 引数の -q を q_name として保存

2) fbjq.conf を読む
    exec_user を取得
    max_process を取得

3) SIGTERM, SIGINT を受信したら g_graceful_stop=true にするシグナルハンドラーを登録する

4) {max_process} の数だけスレッドを生成し、それぞれのスレッドは dequeue へのイベント登録を待機

5) {spool_dir}/queue/{q_name} にあるファイルを走査し a 以降を繰り返す
    a) g_graceful_stop=true ならファイル走査終了
    b) 通常ファイル以外は削除し、次のファイル走査に戻る
    c) ファイルヘッダ(struct request_header_t) を読み magic/cigam を検査
    d) ヘッダ以降のテキスト部を読み exec, args の値を取得する
    e) {exec_user}, {exec}, {args} から "User=", "ExecStart=" を作成し systemd-run と同じ方式の sdbus-c++ 機能でユニットを起動し完了を待機する

6) プログラム終了
*/

namespace {

int main_(int argc, char** argv)
{
    ENTER_FUNCTION();
    constexpr int DEFAULT_MAX_FILES = 50;

    int opt = 0;
    int option_index = 0;

    // ロングオプションの定義
    const struct option long_options[] = {
        {"check",      no_argument,       nullptr, 'C'},
        {"config",     required_argument, nullptr, 'c'},
        {"max-files",  required_argument, nullptr, 'n'},
        {"queue-name", required_argument, nullptr, 'q'},
        {nullptr,      0,                 nullptr, 0  }
    };

    // オプション指定なし（または引数不足）のエラーメッセージを無効化する場合は 0 に設定
    // opterr = 0;

    struct
    {
        int check_only{ 0 };
        const char* cfg_file{ fbjqutil::DEFAULT_CONFIG_FILE };
        int max_files{ DEFAULT_MAX_FILES };
        const char* q_name{ nullptr };

        std::string string() {
            return std::format("check_only={}, cfg_file={}, max_files={}, q_name={}",
                check_only, cfg_file, max_files, NULLABLE_CSTR(q_name));
        }
    }
    app_args;

    while ((opt = getopt_long(argc, argv, "Cc:n:q:", long_options, &option_index)) != -1) {
        switch (opt)
        {
            case 'C':
                app_args.check_only = 1;
                break;

            case 'c':
                app_args.cfg_file = optarg;
                break;

            case 'n':
                app_args.max_files = std::clamp(std::atoi(optarg), 1, 1000);
                break;

            case 'q':
                app_args.q_name = optarg;
                break;

            case '?':
                // 未知のオプション、または引数が不足している場合
                LOG_ERROR("Unknown option or missing argument.");
                return 1;

            default:
                break;
        }
    }

    LOG_INFO("args: {}", app_args.string());

    if (! app_args.q_name) {
        LOG_ERROR("The queue name is a required parameter.");
        return EXIT_FAILURE;
    }

    // 設定ファイルの読み込み
    auto appConfigPtr{ fbjqutil::load_config(app_args.cfg_file) };
    if (appConfigPtr) {
        if (app_args.check_only) {
            LOG_INFO("config check ok");
            return EXIT_SUCCESS;
        }

    } else {
        LOG_ERROR("load_config");
        return EXIT_FAILURE;
    }

    const auto* app_cfg = appConfigPtr.get();

    fbjqutil::queue_item_view_t queue_item;
    if (! fbjqutil::get_queue_item(app_cfg, app_args.q_name, &queue_item)) {
        LOG_ERROR("get_queue_item");
        return EXIT_FAILURE;
    }

    sigset_t sigset;

    // 1. ブロックしたいシグナルのセットを作成
    ::sigemptyset(&sigset);
    ::sigaddset(&sigset, SIGINT);
    ::sigaddset(&sigset, SIGTERM);

    // 2. メインスレッド（および今後生成される全スレッド）でシグナルをブロック
    // pthread_create されるスレッドは、親スレッドの sigmask を継承するため、
    // スレッド生成前に pthread_sigmask を呼ぶのがポイントです。
    if (::pthread_sigmask(SIG_BLOCK, &sigset, NULL) != 0) {
        LOG_ERROR("pthread_sigmask error");
        return EXIT_FAILURE;
    }

    const struct timespec timeout{};

    const auto should_continue = [&](const auto& entry, const int moved) -> bool {
        (void) entry;

        if (moved >= app_args.max_files) {
            // .path の停止を検知するために一定数を処理したら .service を終了する
            LOG_INFO("The maximum number of processes has been reached.");
            return false;
        }

        siginfo_t siginfo;
        const int signo = ::sigtimedwait(&sigset, &siginfo, &timeout);
        if (signo > 0) {
            LOG_INFO("Signal received: {}", signo);
        }

        return true;
    };

    const auto moved = for_each_queue_file(app_cfg, app_args.q_name, should_continue);
    if (moved < 0) {
        LOG_ERROR("for_each_queue_file: moved={}", moved);
        return EXIT_FAILURE;
    }

    LOG_INFO("Validated {} files", moved);

    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv)
{
    ENTER_FUNCTION();

    ::umask(0);

    const int rc = main_(argc, argv);
    LOG_INFO("program return-code={}", rc);

    return rc;
}
