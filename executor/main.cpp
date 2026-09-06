// executor/main.cpp
#include "fbjq-util.hpp"
#include <csignal>
#include <getopt.h>

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

static int main_(int argc, char** argv)
{
    ENTER_FUNCTION();

    return 0;
}

int main(int argc, char** argv)
{
    ENTER_FUNCTION();

    const int rc = main_(argc, argv);
    LOG_INFO("program return-code={}", rc);

    return rc;
}
