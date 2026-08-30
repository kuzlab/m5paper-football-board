#include "net_http.h"

#include <Arduino.h>

#include <cstdlib>
#include <cstring>

namespace fb {
namespace net {
namespace {

// 1行読む (CRLF 終端)。タイムアウトしたら false。
bool read_line(WiFiClientSecure& c, std::string& out, unsigned long timeout_ms) {
  out.clear();
  const unsigned long start = millis();
  while (millis() - start < timeout_ms) {
    if (!c.available()) {
      if (!c.connected()) return false;
      delay(2);
      continue;
    }
    const int ch = c.read();
    if (ch < 0) continue;
    if (ch == '\n') {
      if (!out.empty() && out.back() == '\r') out.pop_back();
      return true;
    }
    out.push_back(static_cast<char>(ch));
    if (out.size() > 2048) return false;  // ヘッダ行としてあり得ない長さ
  }
  return false;
}

std::string lower(const std::string& s) {
  std::string o = s;
  for (auto& c : o) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return o;
}

}  // namespace

// --- BodyStream ---------------------------------------------------------

void BodyStream::attach(WiFiClientSecure* c, long content_length, bool chunked,
                        unsigned long timeout_ms) {
  client_ = c;
  chunked_ = chunked;
  timeout_ms_ = timeout_ms;
  consumed_ = 0;
  peeked_ = -1;
  done_ = false;
  if (chunked_) {
    remaining_ = 0;  // 最初のチャンクヘッダは fill() で読む
  } else if (content_length >= 0) {
    remaining_ = content_length;
    if (remaining_ == 0) done_ = true;
  } else {
    // Content-Length も chunked も無い = 接続クローズまで読む。
    // この場合 keep-alive は諦める。
    remaining_ = -1;
  }
}

int BodyStream::fill() {
  if (!client_ || done_) return -1;

  if (chunked_ && remaining_ == 0) {
    std::string line;
    if (!read_line(*client_, line, timeout_ms_)) {
      done_ = true;
      return -1;
    }
    if (line.empty()) {  // 直前チャンクの末尾 CRLF
      if (!read_line(*client_, line, timeout_ms_)) {
        done_ = true;
        return -1;
      }
    }
    const long sz = strtol(line.c_str(), nullptr, 16);
    if (sz <= 0) {
      // 終端チャンク。トレーラを読み飛ばす。
      while (read_line(*client_, line, timeout_ms_) && !line.empty()) {
      }
      done_ = true;
      return -1;
    }
    remaining_ = sz;
  }

  if (!chunked_ && remaining_ == 0) {
    done_ = true;
    return -1;
  }

  const unsigned long start = millis();
  while (millis() - start < timeout_ms_) {
    if (client_->available()) {
      const int ch = client_->read();
      if (ch < 0) continue;
      if (remaining_ > 0) --remaining_;
      ++consumed_;
      if (chunked_ && remaining_ == 0) {
        // チャンク末尾の CRLF は次の fill() で処理する
      }
      return ch;
    }
    if (!client_->connected()) {
      done_ = true;
      return -1;
    }
    delay(1);
  }
  done_ = true;
  return -1;
}

int BodyStream::read() {
  if (peeked_ >= 0) {
    const int v = peeked_;
    peeked_ = -1;
    return v;
  }
  return fill();
}

int BodyStream::peek() {
  if (peeked_ < 0) peeked_ = fill();
  return peeked_;
}

int BodyStream::available() {
  if (peeked_ >= 0) return 1;
  if (done_) return 0;
  if (!client_) return 0;
  // 実際に残っているかは client_ 依存。0 を返すと ArduinoJson が
  // 早期終了するので、終端が確定するまでは 1 を返す。
  return 1;
}

bool BodyStream::drain() {
  if (!client_) return false;
  peeked_ = -1;
  const unsigned long start = millis();
  while (!done_) {
    if (millis() - start > timeout_ms_ * 2) return false;
    if (fill() < 0) break;
  }
  return done_;
}

// --- KeepAliveClient ----------------------------------------------------

bool KeepAliveClient::begin(const char* host, std::uint16_t port,
                            const char* ca_pem) {
  host_ = host;
  port_ = port;
  ca_pem_ = ca_pem;
  handshakes_ = 0;
  // setInsecure() は使わない (§2.4)。CA を必ず与える。
  if (!ca_pem_ || !*ca_pem_) return false;
  client_.setCACert(ca_pem_);
  client_.setTimeout(15);  // 秒
  return ensure_connected();
}

bool KeepAliveClient::ensure_connected() {
  if (client_.connected()) return true;
  client_.stop();
  ++handshakes_;
  return client_.connect(host_.c_str(), port_);
}

bool KeepAliveClient::connected() { return client_.connected(); }

void KeepAliveClient::end() { client_.stop(); }

bool KeepAliveClient::request(const char* path, const char* token,
                              Response& res, BodyStream& body) {
  res = Response();
  if (!ensure_connected()) return false;

  std::string req;
  req.reserve(512);
  req += "GET ";
  req += path;
  req += " HTTP/1.1\r\nHost: ";
  req += host_;
  req += "\r\nX-Auth-Token: ";
  req += token;
  req += "\r\nAccept: application/json\r\n";
  // gzip が返ると ESP32 側で展開できずストリーミングパースが破綻する。
  req += "Accept-Encoding: identity\r\n";
  req += "Connection: keep-alive\r\n";
  req += "User-Agent: m5paper-football/1.0\r\n\r\n";

  if (client_.print(req.c_str()) != static_cast<int>(req.size())) {
    // 接続が切れていた可能性がある。1度だけ張り直して再送する。
    client_.stop();
    if (!ensure_connected()) return false;
    if (client_.print(req.c_str()) != static_cast<int>(req.size())) return false;
  }

  std::string line;
  if (!read_line(client_, line, 15000)) return false;
  // "HTTP/1.1 200 OK"
  {
    const std::size_t sp = line.find(' ');
    if (sp == std::string::npos) return false;
    res.status = atoi(line.c_str() + sp + 1);
  }

  while (read_line(client_, line, 15000)) {
    if (line.empty()) break;  // ヘッダ終わり
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    const std::string name = lower(line.substr(0, colon));
    std::size_t vs = colon + 1;
    while (vs < line.size() && line[vs] == ' ') ++vs;
    const std::string value = line.substr(vs);

    if (name == "content-length") {
      res.content_length = atol(value.c_str());
    } else if (name == "transfer-encoding") {
      res.chunked = lower(value).find("chunked") != std::string::npos;
    } else if (name == "connection") {
      res.keep_alive = lower(value).find("close") == std::string::npos;
    } else if (name == "x-requests-available-minute") {
      res.minute_remaining = atoi(value.c_str());
    } else if (name == "retry-after" ||
               name == "x-requestcounter-reset") {
      res.retry_after_sec = atoi(value.c_str());
    }
  }

  body.attach(&client_, res.content_length, res.chunked, 8000);
  return true;
}

void KeepAliveClient::finish(BodyStream& body, const Response& res) {
  // ボディを読み切らないと次のリクエストがヘッダとして本文を読んでしまう。
  if (!body.drain() || !res.keep_alive) {
    client_.stop();  // 読み切れなかったら接続を捨てる方が安全
  }
  body.detach();
}

// --- RatePacer ----------------------------------------------------------

void RatePacer::wait_turn() {
  if (interval_ms_ <= 0) {
    last_ms_ = millis();
    started_ = true;
    return;
  }
  if (started_) {
    const unsigned long elapsed = millis() - last_ms_;
    if (elapsed < static_cast<unsigned long>(interval_ms_)) {
      const unsigned long wait = interval_ms_ - elapsed;
      waited_ms_ += wait;
      delay(wait);
    }
  }
  last_ms_ = millis();
  started_ = true;
}

void RatePacer::back_off(int retry_after_sec) {
  // サーバが待てと言った時間に従う。無視して再送しない (§2.5-3)。
  // 指示が無ければ1分。カウンタのリセット周期がそれ以上になることはない。
  unsigned long wait = (retry_after_sec > 0)
                           ? static_cast<unsigned long>(retry_after_sec) * 1000UL
                           : 60000UL;
  if (wait > 90000UL) wait = 90000UL;  // 起床時間が青天井にならないよう上限
  waited_ms_ += wait;
  delay(wait);
  last_ms_ = millis();
  started_ = true;
}

}  // namespace net
}  // namespace fb
