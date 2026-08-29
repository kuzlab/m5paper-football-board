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
  // API-SPORTS のレート制限ヘッダ (§2.3)。無ければ -1。
  int daily_remaining = -1;
  int minute_remaining = -1;
};

class KeepAliveClient {
 public:
  // ca_pem: ルート CA。setInsecure() は使わない (§2.4)。
  bool begin(const char* host, std::uint16_t port, const char* ca_pem);
  void end();
  bool connected();

  // 1リクエスト送って、ヘッダまで読む。成功したら body に本文を接続する。
  // 失敗時は false。呼び出し側は必ず finish() を呼ぶこと。
  bool request(const char* path, const char* api_key, Response& res,
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

// 毎分レート制限を守るためのペーサ (§2.4 改訂)。
// 無料プランは 10 req/min。守らないと 429 で丸ごと失敗する。
class RatePacer {
 public:
  explicit RatePacer(int per_minute) : per_minute_(per_minute) {}
  // 次のリクエストを投げてよくなるまで待つ。
  void wait_turn();
  unsigned long waited_ms() const { return waited_ms_; }

 private:
  static constexpr int kMaxWindow = 32;
  int per_minute_;
  unsigned long stamps_[kMaxWindow] = {0};
  int count_ = 0;
  unsigned long waited_ms_ = 0;
};

}  // namespace net
}  // namespace fb
