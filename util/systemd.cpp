// util/systemd.cpp
#include "fbjq-util.hpp"
#include <sdbus-c++/sdbus-c++.h>

namespace fbjqutil {

bool systemd_unit_method(const std::string& unit_name, const char* method)
{
    ENTER_FUNCTION();
    
    try {
        // 1. システムバスへの接続を作成し、systemdマネージャのD-Busプロキシを生成
        // event_loop_thread は起動せず、手動でイベントを処理する設定にする
        auto proxy{ sdbus::createProxy(
            sdbus::createSystemBusConnection(),
            sdbus::ServiceName{"org.freedesktop.systemd1"},
            sdbus::ObjectPath{"/org/freedesktop/systemd1"},
            sdbus::dont_run_event_loop_thread
        ) };

        sdbus::ObjectPath my_job;
        bool finished = false;
        std::string result;

        // 2. JobRemoved シグナルを受信した際に実行するコールバック関数を定義
        // 引数: (uint32_t id, ObjectPath job, string unit, string result)
        const auto job_removed_handler = [&](uint32_t, const sdbus::ObjectPath& removed_job,
            const std::string&, const std::string& job_result)
        {
            LOG_DEBUG("removed_job={} job={} job_result={}", removed_job.c_str(), my_job.c_str(), job_result);

            if (removed_job == my_job) {
                result = job_result;
                finished = true;

                proxy->getConnection().leaveEventLoop();
            }
        };

        // 3. メソッド呼び出しの「前」に JobRemoved シグナルのハンドラを登録
        // (StartUnit 実行直後に高速でジョブが完了した場合のシグナル全般を取りこぼさないため)
        proxy->uponSignal("JobRemoved")
            .onInterface("org.freedesktop.systemd1.Manager")
            .call(job_removed_handler);

        // 4. systemd1.Manager の StartUnit メソッドを呼び出し、生成されたジョブのパスを取得
        proxy->callMethod(method)
            .onInterface("org.freedesktop.systemd1.Manager")
            .withArguments(unit_name, "replace")
            .storeResultsTo(my_job);

        // 5. 目的の JobRemoved シグナルを受信するまで待機
#if 1
        if (! finished) {
            proxy->getConnection().enterEventLoop();
        }
#else
        while (! finished) {
            proxy->getConnection().processPendingEvent();
        }
#endif

        LOG_INFO("unit={} method={} my_job={} result={}", unit_name, method, my_job.c_str(), result);

        return result == "done";
    }
    /*
    catch (const sdbus::Error& e) {
        std::cerr << "sbus error: " << e.what() << std::endl;
        return false;

    }*/ catch (const std::exception& ex) {
        LOG_ERROR("exception what={}", ex.what());
        return false;

    } catch (...) {
        LOG_ERROR("exception unknown");
        return false;
    }
}

} // namespace fbjqutil
