// util/systemd.cpp
#include "fbjq-common.hpp"
#include <sdbus-c++/sdbus-c++.h>

#define AUTO_WAIT (1)

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

        sdbus::ObjectPath this_job;
        bool finished = false;
        std::string result;

        // 2. JobRemoved シグナルを受信した際に実行するコールバック関数を定義
        // 引数: (uint32_t id, ObjectPath job, string unit, string result)
        const auto on_signal = [&](uint32_t job_id, const sdbus::ObjectPath& removed_job,
            const std::string& removed_unit, const std::string& job_result)
        {
            LOG_DEBUG("job_id={} removed_job={} removed_unit={} job_result={} this_job={}",
                job_id, removed_job.c_str(), removed_unit, job_result, this_job.c_str());

            if (removed_job == this_job) {
                result = job_result;
                finished = true;

#if defined(AUTO_WAIT)
                proxy->getConnection().leaveEventLoop();
#endif
            }
        };

        // 3. メソッド呼び出しの「前」に JobRemoved シグナルのハンドラを登録
        // (StartUnit 実行直後に高速でジョブが完了した場合のシグナル全般を取りこぼさないため)
        proxy->uponSignal("JobRemoved")
            .onInterface("org.freedesktop.systemd1.Manager")
            .call(on_signal);

        // 4. systemd1.Manager の メソッドを呼び出し、生成されたジョブのパスを取得
        proxy->callMethod(method)
            .onInterface("org.freedesktop.systemd1.Manager")
            .withArguments(unit_name, "replace")
            .storeResultsTo(this_job);

        // 5. 目的の JobRemoved シグナルを受信するまで待機
#if defined(AUTO_WAIT)
        if (! finished) {
            proxy->getConnection().enterEventLoop();
        }

#else
        while (! finished) {
            proxy->getConnection().processPendingEvent();
        }
#endif

        LOG_INFO("unit={} method={} this_job={} result={}", unit_name, method, this_job.c_str(), result);

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
