// microSD と NVS。電源ラッチ方式ではコールドブートなので、持ち越したい状態は
// すべてここを通す (§6.2-1)。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/budget.h"
#include "core/config_parse.h"
#include "core/model.h"

namespace fb {
namespace storage {

// SD のパス
constexpr const char* kConfigPath       = "/config.json";
constexpr const char* kCompetitionsPath = "/competitions.json";
constexpr const char* kLeagueIdsPath    = "/cache/league_ids.json";
constexpr const char* kStandingsPath    = "/cache/standings_prev.json";
constexpr const char* kBudgetPath       = "/cache/budget.json";
constexpr const char* kSeenPath         = "/cache/seen_fixtures.json";
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

// --- 予算 (§2.3) --------------------------------------------------------
// SD を優先し、SD が読めなければ NVS のミラーを使う。SD が抜かれている間に
// 上限が無効化されると API 枠を焼き切るので、二重に持つ。
bool load_budget(BudgetState& s);
bool save_budget(const BudgetState& s);

// --- 前回順位表 (§2.5) --------------------------------------------------
bool load_standings(const std::vector<Competition>& comps,
                    std::vector<LeagueStandings>& out);
bool save_standings(const std::vector<LeagueStandings>& in,
                    const std::vector<Competition>& comps);

// --- 既出 fixture (再掲の優先度下げ用、§2.2 の72時間ウィンドウ) ---------
bool load_seen_fixtures(std::vector<long>& out);
bool save_seen_fixtures(const std::vector<long>& in, std::size_t max_keep = 120);

// --- リーグ ID キャッシュ (§2.2) ----------------------------------------
bool load_league_ids(LeagueIdCache& out);
bool save_league_ids(const LeagueIdCache& in);

}  // namespace storage
}  // namespace fb
