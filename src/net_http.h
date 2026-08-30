// HTTPS keep-alive クライアント (§2.4)。
//
// 13回のリクエストを毎回 TLS ハンドシェイクするとハンドシェイクだけで
// 数十秒かかるため、コネクションを維持して同一接続上に流す。
// keep-alive を維持するにはレスポンスボディを最後の1バイトまで読み切る
// 必要があるので、パース後に必ず drain() する。
#pragma once

#include <Stream.h>
#include <WiFiClientSecure.h>

#include <cstdint>
#include <string>

namespace fb {
namespace net {

// レスポンスボディを Stream として読ませる。identity と chunked の両方を扱う。
// ArduinoJson にそのまま渡せる。
class BodyStream : public Stream {
 public:
  void attach(WiFiClientSecure* c, long content_length, bool chunked,
              unsigned long timeout_ms);
  void detach() { client_ = nullptr; }

  int available() override;
  int read() override;
  int peek() override;
  void flush() override {}
  std::size_t write(std::uint8_t) override { return 0; }

  // 残りを読み捨てる。keep-alive を維持するために必須。
  bool drain();
  bool complete() const { return done_; }
  long consumed() const { return consumed_; }

 private:
  int fill();  // 次のバイトを1つ確保する。-1 で終端/失敗

  WiFiClientSecure* client_ = nullptr;
  bool chunked_ = false;
  bool done_ = false;
  long remaining_ = 0;      // identity: 残バイト / chunked: 現チャンクの残り
  long consumed_ = 0;
  unsigned long timeout_ms_ = 8000;
  int peeked_ = -1;
};

struct Response {
  int status = 0;
  bool chunked = false;
  long content_length = -1;
  bool keep_alive = true;
  // football-data.org のレート制限ヘッダ (§2.5)。無ければ -1 / 0。
  int minute_remaining = -1;  // X-Requests-Available-Minute
  int retry_after_sec = 0;    // 429 のとき Retry-After / X-RequestCounter-Reset
};

class KeepAliveClient {
 public:
  // ca_pem: ルート CA。setInsecure() は使わない (§2.4)。
  bool begin(const char* host, std::uint16_t port, const char* ca_pem);
  void end();
  bool connected();

  // 1リクエスト送って、ヘッダまで読む。成功したら body に本文を接続する。
  // 失敗時は false。呼び出し側は必ず finish() を呼ぶこと。
  // token は X-Auth-Token ヘッダに入れる (football-data.org)。
  bool request(const char* path, const char* token, Response& res,
               BodyStream& body);
  // ボディを読み切り、次のリクエストに備える。
  void finish(BodyStream& body, const Response& res);

  int handshakes() const { return handshakes_; }

 private:
  bool ensure_connected();

  WiFiClientSecure client_;
  std::string host_;
  std::uint16_t port_ = 443;
  const char* ca_pem_ = nullptr;
  int handshakes_ = 0;
};

// レート制限を守るためのペーサ (§2.5)。
// 無料枠は 10 req/min。上限ぴったりを狙わず、リクエスト間に一定の
// ウェイト (既定7秒 = 約8.5 req/min) を必ず入れる。
// keep-alive 接続はウェイト中も維持したままにする (§2.6)。
class RatePacer {
 public:
  explicit RatePacer(int min_interval_ms) : interval_ms_(min_interval_ms) {}
  // 前回のリクエストから interval_ms_ 経つまで待つ。初回は待たない。
  void wait_turn();
  // 429 を受けたときに、サーバの指示ぶんだけ余分に待つ (§2.5-3)。
  void back_off(int retry_after_sec);
  unsigned long waited_ms() const { return waited_ms_; }

 private:
  int interval_ms_;
  unsigned long last_ms_ = 0;
  bool started_ = false;
  unsigned long waited_ms_ = 0;
};

}  // namespace net
}  // namespace fb
