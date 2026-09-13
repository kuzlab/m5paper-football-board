// microSD と NVS。電源ラッチ方式ではコールドブートなので、持ち越したい状態は
// すべてここを通す (§6.2-1)。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/config_parse.h"
#include "core/freshness.h"
#include "core/model.h"
#include "core/selector.h"

namespace fb {
namespace storage {

// SD のパス
constexpr const char* kConfigPath       = "/config.json";
constexpr const char* kCompetitionsPath = "/competitions.json";
constexpr const char* kStandingsPath    = "/cache/standings_prev.json";
constexpr const char* kSeenPath         = "/cache/seen_fixtures.json";
constexpr const char* kPlanPath         = "/cache/last_plan.json";
constexpr const char* kLogDir           = "/logs";
constexpr const char* kFont28Path       = "/fonts/board_28.vlw";
constexpr const char* kFont20Path       = "/fonts/board_20.vlw";

bool begin_sd();
bool sd_ready();

bool read_file(const char* path, std::string& out, std::size_t max_bytes = 65536);
bool write_file_atomic(const char* path, const std::string& data);
bool ensure_dir(const char* path);

// --- NVS (§5.5, §8.1) ---------------------------------------------------
bool begin_nvs();
std::uint32_t last_plan_hash();
void set_last_plan_hash(std::uint32_t h);
int partial_refresh_count();
void set_partial_refresh_count(int n);
int consecutive_failures();
void set_consecutive_failures(int n);
std::time_t last_fetch_utc();
void set_last_fetch_utc(std::time_t t);
std::time_t last_success_utc();
void set_last_success_utc(std::time_t t);

// --- 前回順位表 (§2.5) --------------------------------------------------
bool load_standings(const std::vector<Competition>& comps,
                    std::vector<LeagueStandings>& out);
bool save_standings(const std::vector<LeagueStandings>& in,
                    const std::vector<Competition>& comps);

// --- 前回描画した画面 (§6.2-1) ------------------------------------------
// 電源ラッチ方式ではコールドブートなので EPD のフレームバッファが空で
// 始まる。パネルの絵を再現するには、前回描いた内容を持ち越すしかない。
bool load_plan(RenderPlan& out);
bool save_plan(const RenderPlan& in);

// --- 新着判定の記録 -----------------------------------------------------
// 試合ごとに「初めて完了を確認した時刻」。旧形式は記録を捨てて履歴なし扱い。
// 欠損・破損は false を返す (エラーにはしない)。
bool load_seen_log(SeenLog& out);
bool save_seen_log(const SeenLog& in);

}  // namespace storage
}  // namespace fb
