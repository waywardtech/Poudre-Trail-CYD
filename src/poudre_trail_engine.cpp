#include "poudre_trail_engine.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <sstream>

// ─── CSV helpers ─────────────────────────────────────────────────────────────

static std::string trimStr(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b - a);
}

static std::string lowerStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

static std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQ = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        if (ch == '"') {
            if (inQ && i+1 < line.size() && line[i+1] == '"') { cur += '"'; ++i; }
            else inQ = !inQ;
        } else if (ch == ',' && !inQ) {
            out.push_back(trimStr(cur)); cur.clear();
        } else {
            cur += ch;
        }
    }
    out.push_back(trimStr(cur));
    return out;
}

static std::vector<std::vector<std::string>> parseCsv(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (trimStr(line).empty() || line[0] == '#') continue;
        rows.push_back(splitCsvLine(line));
    }
    return rows;
}

static int csvInt(const std::string& s, int fb = 0) {
    if (s.empty()) return fb;
    try { return std::stoi(s); } catch (...) { return fb; }
}

static bool csvBool(const std::string& s) {
    std::string t = lowerStr(trimStr(s));
    return t == "1" || t == "true" || t == "yes";
}

static std::map<std::string, std::string> parseKV(const std::string& text) {
    std::map<std::string, std::string> kv;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trimStr(line);
        if (line.empty() || line[0] == '#') continue;
        auto p = line.find('=');
        if (p == std::string::npos) continue;
        kv[trimStr(line.substr(0, p))] = trimStr(line.substr(p+1));
    }
    return kv;
}

static int kvInt(const std::map<std::string,std::string>& kv, const std::string& k, int fb) {
    auto it = kv.find(k);
    return it == kv.end() ? fb : csvInt(it->second, fb);
}
static std::vector<std::string> splitStr(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == sep) { out.push_back(trimStr(cur)); cur.clear(); }
        else cur += ch;
    }
    out.push_back(trimStr(cur));
    return out;
}

// CSV column layout for choices (14 columns each, starting at offset):
// 0:label 1:food 2:ammo 3:med 4:trade 5:horse 6:morale 7:health
// 8:repJanis 9:repFriday 10:repMason 11:setFlag(s) 12:result 13:days
static const int CHOICE_COLS = 14;

static Choice parseChoice(const std::vector<std::string>& row, int off) {
    Choice c;
    if ((int)row.size() < off + CHOICE_COLS) return c;
    c.label               = row[off+0];
    c.foodDelta           = csvInt(row[off+1]);
    c.ammunitionDelta     = csvInt(row[off+2]);
    c.medicineDelta       = csvInt(row[off+3]);
    c.tradeGoodsDelta     = csvInt(row[off+4]);
    c.horseConditionDelta = csvInt(row[off+5]);
    c.moraleDelta         = csvInt(row[off+6]);
    c.healthDelta         = csvInt(row[off+7]);
    c.repJanisDelta       = csvInt(row[off+8]);
    c.repFridayDelta      = csvInt(row[off+9]);
    c.repMasonDelta       = csvInt(row[off+10]);
    c.setFlag             = row[off+11];
    c.resultText          = row[off+12];
    c.daysDelta           = csvInt(row[off+13]);
    return c;
}

// Trades CSV: locationId,label,food,ammo,med,trade,horse,morale,health,repJ,repFr,repMa,result
static TradeOffer parseTrade(const std::vector<std::string>& row) {
    TradeOffer t;
    if (row.size() < 13) return t;
    t.locationId      = row[0];
    t.label           = row[1];
    t.foodDelta       = csvInt(row[2]);
    t.ammunitionDelta = csvInt(row[3]);
    t.medicineDelta   = csvInt(row[4]);
    t.tradeGoodsDelta = csvInt(row[5]);
    t.horseDelta      = csvInt(row[6]);
    t.moraleDelta     = csvInt(row[7]);
    t.healthDelta     = csvInt(row[8]);
    t.repJanisDelta   = csvInt(row[9]);
    t.repFridayDelta  = csvInt(row[10]);
    t.repMasonDelta   = csvInt(row[11]);
    t.resultText      = row[12];
    return t;
}

// ─── Engine ──────────────────────────────────────────────────────────────────

static const int FOOD_PER_DAY      = 9;
static const int TRAIL_EVENT_CHANCE = 55;   // percent per leg
static const int HUNT_SUCCESS       = 70;   // percent
static const int HUNT_AMMO          = 3;

GameEngine::GameEngine() {
    if (!loadEmbeddedWorld()) buildMinimalWorld();
}

void GameEngine::buildMinimalWorld() {
    locations_.clear();
    events_.clear();
    trades_.clear();
    route_.clear();
    Location a; a.id = "start"; a.name = "Laporte"; a.summary = "No game data found."; a.mapX = 20; a.mapY = 50;
    Location b; b.id = "end"; b.name = "Fort Collins"; b.summary = "Journey's end."; b.milesFromStart = 10; b.mapX = 80; b.mapY = 50;
    locations_ = {a, b};
    route_ = {"start", "end"};
}

bool GameEngine::loadFromText(const std::string& locCsv,
                               const std::string& evCsv,
                               const std::string& trCsv,
                               const BodyReader& readBody,
                               std::string* err) {
    auto locRows = parseCsv(locCsv);
    auto evRows  = parseCsv(evCsv);
    auto trRows  = parseCsv(trCsv);

    if (locRows.size() < 3 || evRows.size() < 2) {
        if (err) *err = "CSV missing headers or data";
        return false;
    }
    if (evRows[0].size() < 7 + (size_t)CHOICE_COLS) {
        if (err) *err = "events.csv is an older format";
        return false;
    }

    std::vector<Location> locs;
    std::vector<Event> evs;
    std::map<std::string, std::vector<TradeOffer>> trades;

    // locations: id,name,summary,milesFromStart,hasTradingPost,image[,mapX,mapY]
    for (size_t i = 1; i < locRows.size(); ++i) {
        const auto& r = locRows[i];
        if (r.size() < 6) continue;
        Location loc;
        loc.id             = r[0];
        loc.name           = r[1];
        loc.summary        = r[2];
        loc.milesFromStart = csvInt(r[3]);
        loc.hasTradingPost = csvBool(r[4]);
        loc.image          = r[5];
        if (r.size() >= 8) { loc.mapX = csvInt(r[6], 50); loc.mapY = csvInt(r[7], 50); }
        locs.push_back(loc);
    }

    // events: id,locationId,title,bodyFile,image,kind,requiresFlag, then 14-column choices
    for (size_t i = 1; i < evRows.size(); ++i) {
        const auto& r = evRows[i];
        if (r.size() < 7) continue;
        Event ev;
        ev.id           = r[0];
        ev.locationId   = r[1];
        ev.title        = r[2];
        ev.bodyFile     = r[3];
        ev.imagePath    = r[4];
        ev.kind         = r[5].empty() ? "story" : lowerStr(r[5]);
        ev.requiresFlag = r[6];
        if (ev.kind.compare(0, 6, "timed:") == 0) ev.minDay = csvInt(ev.kind.substr(6), 0);
        for (int off = 7; off + CHOICE_COLS <= (int)r.size(); off += CHOICE_COLS) {
            Choice c = parseChoice(r, off);
            if (!trimStr(c.label).empty()) ev.choices.push_back(c);
        }
        if (ev.choices.empty()) continue;
        if (readBody) ev.body = trimStr(readBody(ev.bodyFile));
        if (ev.body.empty()) ev.body = ev.title;
        evs.push_back(ev);
    }

    for (size_t i = 1; i < trRows.size(); ++i) {
        TradeOffer t = parseTrade(trRows[i]);
        if (!trimStr(t.label).empty()) trades[t.locationId].push_back(t);
    }

    if (locs.size() < 2 || evs.empty()) {
        if (err) *err = "No usable locations or events after parse";
        return false;
    }

    locations_ = locs;
    events_    = evs;
    trades_    = trades;
    route_.clear();
    for (const auto& l : locations_) route_.push_back(l.id);
    routeIndex_ = 0;
    mode_ = GameMode::TITLE;
    currentEvent_ = nullptr;
    pending_.clear();
    return true;
}

#if __has_include("world_data.hpp")
#include "world_data.hpp"
bool GameEngine::loadEmbeddedWorld() {
    auto reader = [](const std::string& path) -> std::string {
        for (const WorldBody& b : WORLD_BODIES)
            if (path == b.path) return b.text;
        return std::string();
    };
    return loadFromText(WORLD_LOCATIONS_CSV, WORLD_EVENTS_CSV, WORLD_TRADES_CSV, reader);
}
#else
bool GameEngine::loadEmbeddedWorld() { return false; }
#endif

const Location* GameEngine::findLocation(const std::string& id) const {
    for (const auto& loc : locations_)
        if (loc.id == id) return &loc;
    return nullptr;
}

const Event* GameEngine::findEvent(const std::string& id) const {
    for (const auto& ev : events_)
        if (ev.id == id) return &ev;
    return nullptr;
}

bool GameEngine::hasFlag(const std::string& f) const { return flags_.count(f) > 0; }

// "a&!b&c|d": AND of groups; a group is an OR of atoms; an atom may start with '!'.
bool GameEngine::flagExpr(const std::string& expr) const {
    if (trimStr(expr).empty()) return true;
    for (const std::string& group : splitStr(expr, '&')) {
        if (group.empty()) continue;
        bool any = false;
        for (std::string atom : splitStr(group, '|')) {
            bool neg = !atom.empty() && atom[0] == '!';
            if (neg) atom = trimStr(atom.substr(1));
            if (hasFlag(atom) != neg) { any = true; break; }
        }
        if (!any) return false;
    }
    return true;
}

bool GameEngine::eventEligible(const Event& ev) const {
    return flagExpr(ev.requiresFlag);
}

uint32_t GameEngine::nextRand() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

std::string GameEngine::dateLabel() const {
    static const char* months[] = {"May", "Jun", "Jul", "Aug", "Sep", "Oct"};
    static const int   mdays[]  = {31, 30, 31, 31, 30, 31};
    int d = 12 + (day_ - 1);   // Day 1 = May 12, 1864
    int m = 0;
    while (m < 5 && d > mdays[m]) { d -= mdays[m]; ++m; }
    return std::string(months[m]) + " " + std::to_string(d) + ", 1864";
}

void GameEngine::startGame() {
    resources_ = Resources{};
    flags_.clear();
    seen_.clear();
    pending_.clear();
    routeIndex_ = 0;
    day_ = 1;
    currentEvent_ = nullptr;
    currentTrades_.clear();
    resultText_.clear();
    gameOverReason_.clear();
    endingTitle_.clear();
    endingBody_.clear();
    footer_ = "Laporte, May 1864.";
    mode_ = GameMode::TRAVEL;
    arriveAtStop(false);
}

void GameEngine::arriveAtStop(bool rollTrailEvent) {
    pending_.clear();
    currentEvent_ = nullptr;
    const bool last = routeIndex_ + 1 == route_.size();

    if (rollTrailEvent && !last && (int)(nextRand() % 100) < TRAIL_EVENT_CHANCE) {
        std::vector<const Event*> pool;
        for (const auto& ev : events_)
            if (ev.kind == "trail" && !seen_.count(ev.id) && eventEligible(ev)) pool.push_back(&ev);
        if (!pool.empty()) pending_.push_back(pool[nextRand() % pool.size()]->id);
    }
    for (const auto& ev : events_)
        if (ev.minDay > 0 && day_ >= ev.minDay && !seen_.count(ev.id)) pending_.push_back(ev.id);
    for (const auto& ev : events_)
        if (ev.minDay == 0 && ev.kind != "trail" && ev.locationId == route_[routeIndex_] && !seen_.count(ev.id))
            pending_.push_back(ev.id);

    advanceQueue();
}

void GameEngine::advanceQueue() {
    while (!pending_.empty()) {
        std::string id = pending_.front();
        pending_.pop_front();
        const Event* ev = findEvent(id);
        if (!ev || seen_.count(id) || !eventEligible(*ev)) continue;   // flags may have changed
        currentEvent_ = ev;
        mode_ = GameMode::EVENT;
        footer_ = "An encounter unfolds.";
        return;
    }
    currentEvent_ = nullptr;
    if (routeIndex_ + 1 == route_.size()) { finishJourney(); return; }
    mode_ = GameMode::TRAVEL;
    footer_ = "Where next?";
}

void GameEngine::advanceDays(int n) {
    if (n <= 0) return;
    day_ += n;
    resources_.food -= FOOD_PER_DAY * n;
    if (resources_.food < 25) resources_.health -= 3 * n;   // hunger wears people down
}

void GameEngine::travel() {
    if (mode_ != GameMode::TRAVEL) return;
    if (routeIndex_ + 1 < route_.size()) travelTo(route_[routeIndex_ + 1]);
}

void GameEngine::travelTo(const std::string& nextLocId) {
    if (mode_ == GameMode::VICTORY || mode_ == GameMode::GAME_OVER) return;
    auto it = std::find(route_.begin(), route_.end(), nextLocId);
    if (it == route_.end()) return;

    size_t idx = (size_t)std::distance(route_.begin(), it);
    int miles = std::abs(locations_[idx].milesFromStart - locations_[routeIndex_].milesFromStart);
    int days  = std::max(1, (miles + 4) / 5);

    advanceDays(days);
    resources_.horseCondition -= 2 * days;
    resources_.morale         -= 1;
    clampResources();
    if (mode_ == GameMode::GAME_OVER) return;

    routeIndex_ = idx;
    arriveAtStop(true);
}

void GameEngine::applyDeltas(int food, int ammo, int med, int trade, int horse, int morale,
                             int health, int rj, int rf, int rm) {
    resources_.food           += food;
    resources_.ammunition     += ammo;
    resources_.medicine       += med;
    resources_.tradeGoods     += trade;
    resources_.horseCondition += horse;
    resources_.morale         += morale;
    resources_.health         += health;
    resources_.repJanis       += rj;
    resources_.repFriday      += rf;
    resources_.repMason       += rm;
}

std::string GameEngine::deltaSummary(const Resources& b) const {
    std::string out;
    auto add = [&](const char* name, int before, int after) {
        if (before == after) return;
        if (!out.empty()) out += ", ";
        out += name;
        out += (after > before ? " +" : " -");
        out += std::to_string(std::abs(after - before));
    };
    add("Food", b.food, resources_.food);
    add("Ammo", b.ammunition, resources_.ammunition);
    add("Medicine", b.medicine, resources_.medicine);
    add("Trade goods", b.tradeGoods, resources_.tradeGoods);
    add("Horses", b.horseCondition, resources_.horseCondition);
    add("Morale", b.morale, resources_.morale);
    add("Health", b.health, resources_.health);
    return out;
}

void GameEngine::chooseOption(int index) {
    if (mode_ == GameMode::EVENT) {
        if (!currentEvent_ || index < 0 || (size_t)index >= currentEvent_->choices.size()) return;
        const Choice& c = currentEvent_->choices[index];
        Resources before = resources_;
        applyDeltas(c.foodDelta, c.ammunitionDelta, c.medicineDelta, c.tradeGoodsDelta,
                    c.horseConditionDelta, c.moraleDelta, c.healthDelta,
                    c.repJanisDelta, c.repFridayDelta, c.repMasonDelta);
        advanceDays(c.daysDelta);
        for (const std::string& f : splitStr(c.setFlag, ','))
            if (!f.empty()) flags_.insert(f);
        seen_.insert(currentEvent_->id);
        resultTitle_ = currentEvent_->title;
        resultImage_ = currentEvent_->imagePath;
        currentEvent_ = nullptr;
        clampResources();
        evaluateReputation();
        resultText_ = c.resultText.empty() ? "The road goes on." : c.resultText;
        std::string d = deltaSummary(before);
        if (!d.empty()) resultText_ += "\n" + d;
        if (mode_ != GameMode::GAME_OVER) { mode_ = GameMode::RESULT; footer_ = "Tap Continue."; }
    } else if (mode_ == GameMode::RESULT) {
        advanceQueue();
    } else if (mode_ == GameMode::TRADE) {
        if (index < 0 || (size_t)index >= currentTrades_.size()) {
            footer_ = "You leave the trading post.";
            mode_ = GameMode::TRAVEL;
            return;
        }
        const TradeOffer& t = currentTrades_[index];
        if (resources_.tradeGoods + t.tradeGoodsDelta < 0) {
            footer_ = "Not enough trade goods.";
            return;
        }
        applyDeltas(t.foodDelta, t.ammunitionDelta, t.medicineDelta, t.tradeGoodsDelta,
                    t.horseDelta, t.moraleDelta, t.healthDelta,
                    t.repJanisDelta, t.repFridayDelta, t.repMasonDelta);
        footer_ = t.resultText.empty() ? "Trade completed." : t.resultText;
        clampResources();
        evaluateReputation();
        // stay at the post so several trades can be made; "Leave" returns to the road
    } else if (mode_ == GameMode::CAMP) {
        Resources before = resources_;
        if (index == 0) {                                  // rest
            advanceDays(1);
            applyDeltas(0, 0, 0, 0, +8, +3, +8, 0, 0, 0);
            resultText_ = "A day of rest. The horses graze, the wagon gets mended, and nobody has to be anywhere.";
        } else if (index == 1) {                           // hunt
            if (resources_.ammunition < HUNT_AMMO) { footer_ = "Not enough ammunition."; return; }
            advanceDays(1);
            resources_.ammunition -= HUNT_AMMO;
            if ((int)(nextRand() % 100) < HUNT_SUCCESS) {
                resources_.food += 16; resources_.morale += 2;
                resultText_ = "Quiet work and a steady hand. You carry meat back to camp.";
            } else {
                resources_.food += 3; resources_.morale -= 2;
                resultText_ = "The game kept its distance all day. You return with little but sore feet.";
            }
        } else if (index == 3) {
            closeCamp();
            return;
        } else {
            return;
        }
        resultTitle_ = "Camp";
        resultImage_ = "art/night_camp.gif";
        clampResources();
        std::string d = deltaSummary(before);
        if (!d.empty()) resultText_ += "\n" + d;
        if (mode_ != GameMode::GAME_OVER) { mode_ = GameMode::RESULT; footer_ = "Tap Continue."; }
    }
}

void GameEngine::openTrade() {
    if (mode_ != GameMode::TRAVEL) return;
    currentTrades_.clear();
    auto it = trades_.find(route_[routeIndex_]);
    if (it == trades_.end() || it->second.empty()) {
        footer_ = "No trading post here.";
        return;
    }
    currentTrades_ = it->second;
    mode_ = GameMode::TRADE;
    footer_ = "Choose a trade.";
}

void GameEngine::openCamp() {
    if (mode_ != GameMode::TRAVEL) return;
    mode_ = GameMode::CAMP;
    footer_ = "Camp.";
}

void GameEngine::closeCamp() {
    if (mode_ == GameMode::CAMP) { mode_ = GameMode::TRAVEL; footer_ = "Where next?"; }
}

void GameEngine::evaluateReputation() {
    if (resources_.repMason >= 5 && resources_.repFriday >= 5)
        flags_.insert("moral_contradiction");
}

void GameEngine::clampResources() {
    resources_.food          = std::max(0, resources_.food);
    resources_.ammunition    = std::max(0, resources_.ammunition);
    resources_.medicine      = std::max(0, resources_.medicine);
    resources_.tradeGoods    = std::max(0, resources_.tradeGoods);
    resources_.horseCondition= std::max(0, std::min(100, resources_.horseCondition));
    resources_.morale        = std::max(0, std::min(100, resources_.morale));
    resources_.health        = std::max(0, std::min(100, resources_.health));
    if (mode_ == GameMode::GAME_OVER || mode_ == GameMode::VICTORY) return;
    const char* why = nullptr;
    if      (resources_.food == 0)           why = "The food ran out miles from anyone who could spare more. Your household can go no farther.";
    else if (resources_.health == 0)         why = "Sickness and exhaustion finished what the road started. Your household can go no farther.";
    else if (resources_.horseCondition == 0) why = "The horses gave out on the road. Without them the wagon, and the journey, ends here.";
    else if (resources_.morale == 0)         why = "Nobody has the heart for another mile. Your household turns back toward Laporte.";
    if (why) {
        mode_ = GameMode::GAME_OVER;
        gameOverReason_ = why;
        footer_ = "Your household can go no farther.";
    }
}

std::string GameEngine::repLabel(int v, const std::string& npc) const {
    if (npc == "Mason") {
        if (v >= 12) return "Trusted";
        if (v >= 5)  return "Useful";
        if (v >= 0)  return "Watching";
        if (v >= -5) return "Wary";
        return "Closed";
    }
    if (v >= 12) return "Trusting";
    if (v >= 5)  return "Open";
    if (v >= 0)  return "Neutral";
    if (v >= -5) return "Wary";
    return "Closed";
}

void GameEngine::finishJourney() {
    mode_ = GameMode::VICTORY;
    std::string t, b;

    if (hasFlag("map_true")) {
        t = "A Map With Names";
        b = "Ames wrote the river's names into the margin and credited the people who gave them. It is a smaller victory than it sounds, and a real one.";
    } else if (hasFlag("map_mason")) {
        t = "Mason's Line";
        b = "Mason's survey became the map. It is tidy and confident and wrong in the ways that matter, and it will be quoted for a hundred years.";
    } else if (hasFlag("map_partial")) {
        t = "A Partial Map";
        b = "Some names fit on the paper and most stayed with the people who knew them. Maps are always incomplete; this one knows it.";
    } else if (hasFlag("map_blank")) {
        t = "The Blank Places";
        b = "You left the blanks blank. Whether that protected the names or erased them, no one will know for years.";
    } else {
        t = "Arrival";
        b = "You reached Fort Collins with your household and your own opinion of the road.";
    }

    if (hasFlag("moral_contradiction"))
        b += " You carried both ledgers: Mason's papers and Friday's trust. Survival did not keep your hands entirely clean.";
    else if (resources_.repFriday >= 5)
        b += " Friday's people will remember that you asked before you walked.";
    else if (resources_.repMason >= 5)
        b += " Mason's paper already carries your name.";
    if (hasFlag("helped_flood"))
        b += " The people you pulled from the bank at the flood have not forgotten it.";
    if (hasFlag("sold_point"))
        b += " The stone point went to a collector in Denver. You think of it more than you expected.";
    else if (hasFlag("left_point"))
        b += " The stone point still lies in its gravel, edge up, waiting for rain.";
    if (hasFlag("carries_letter"))
        b += " Virginia's letter is still in your coat. You will post it.";

    b += " History: the May flood took Camp Collins from Laporte; in August the army raised Fort Collins here. In November, at Sand Creek, soldiers killed more than a hundred Cheyenne and Arapaho, mostly women and children. The old names survive in the people who kept them.";

    endingTitle_ = t;
    endingBody_  = b;
    footer_ = "Journey complete.";
}

Snapshot GameEngine::snapshot() const {
    Snapshot s;
    s.mode = mode_;
    s.resources = resources_;
    s.day = day_;
    s.dateLabel = dateLabel();
    s.locationCount = (int)route_.size();

    if (!route_.empty()) {
        s.locationIndex = (int)routeIndex_;
        s.locationId = route_[routeIndex_];
        const Location* loc = findLocation(s.locationId);
        if (loc) {
            s.locationName = loc->name;
            s.locationSummary = loc->summary;
            s.imagePath = loc->image;
        }
    }

    s.footer = footer_;
    s.reputationSummaryJanis  = repLabel(resources_.repJanis,  "Janis");
    s.reputationSummaryFriday = repLabel(resources_.repFriday, "Friday");
    s.reputationSummaryMason  = repLabel(resources_.repMason,  "Mason");

    switch (mode_) {
    case GameMode::TITLE:
        s.headline = "Poudre Trail";
        s.body = "Cache la Poudre valley, May 1864. The river is rising, the army is coming, and every map is a promise to someone. Who draws it when the water falls?";
        s.imagePath = "art/title.gif";
        s.optionLabels = {"Start Journey", "Load Save"};
        break;
    case GameMode::TRAVEL:
        s.headline = s.locationName;
        s.body = s.locationSummary;
        s.optionLabels = {"Travel onward", "Trade / barter", "Camp: rest or hunt", "Save game"};
        break;
    case GameMode::CAMP:
        s.headline = "Camp";
        s.body = "Resting restores health, horses and spirits, but costs a day of food. Hunting costs 3 ammunition and a day, and may find game.";
        s.imagePath = "art/night_camp.gif";
        s.optionLabels = {"Rest a day", "Hunt (3 ammo)", "Load saved game", "Back"};
        break;
    case GameMode::EVENT:
        if (currentEvent_) {
            s.headline = currentEvent_->title;
            s.body = currentEvent_->body;
            if (!currentEvent_->imagePath.empty()) s.imagePath = currentEvent_->imagePath;
            for (const auto& c : currentEvent_->choices) s.optionLabels.push_back(c.label);
        }
        break;
    case GameMode::RESULT:
        s.headline = resultTitle_;
        s.body = resultText_;
        if (!resultImage_.empty()) s.imagePath = resultImage_;
        s.optionLabels = {"Continue"};
        break;
    case GameMode::TRADE:
        s.headline = "Trading Post";
        s.body = "Supplies and favors are priced differently in each place. Spend carefully.";
        for (const auto& t : currentTrades_) s.optionLabels.push_back(t.label);
        s.optionLabels.push_back("Leave trading post");
        break;
    case GameMode::GAME_OVER:
        s.headline = "Journey Lost";
        s.body = gameOverReason_;
        s.imagePath = "art/night_camp.gif";
        s.optionLabels = {"Restart", "Load Save"};
        break;
    case GameMode::VICTORY:
        s.headline = endingTitle_;
        s.body = endingBody_;
        s.imagePath = "art/fort_collins.gif";
        s.optionLabels = {"Restart", "Load Save"};
        break;
    }
    return s;
}

std::string GameEngine::debugState() const {
    std::ostringstream o;
    o << "mode=" << (int)mode_ << " stop=" << (routeIndex_ + 1) << "/" << route_.size()
      << " day=" << day_ << " rng=" << rng_ << " seen=" << seen_.size()
      << " pending=" << pending_.size() << " flags=";
    bool first = true;
    for (const auto& f : flags_) { o << (first ? "" : ",") << f; first = false; }
    return o.str();
}

std::string GameEngine::serializeSave() const {
    std::ostringstream o;
    o << "ver=2\n";
    o << "food="           << resources_.food          << "\n";
    o << "ammunition="     << resources_.ammunition    << "\n";
    o << "medicine="       << resources_.medicine      << "\n";
    o << "health="         << resources_.health        << "\n";
    o << "morale="         << resources_.morale        << "\n";
    o << "horseCondition=" << resources_.horseCondition<< "\n";
    o << "tradeGoods="     << resources_.tradeGoods    << "\n";
    o << "repJanis="       << resources_.repJanis      << "\n";
    o << "repFriday="      << resources_.repFriday     << "\n";
    o << "repMason="       << resources_.repMason      << "\n";
    o << "routeIndex="     << routeIndex_              << "\n";
    o << "day="            << day_                     << "\n";
    o << "rng="            << rng_                     << "\n";
    o << "flags=";
    { bool first = true; for (const auto& f : flags_) { o << (first ? "" : ",") << f; first = false; } }
    o << "\nseen=";
    { bool first = true; for (const auto& e : seen_)  { o << (first ? "" : ",") << e; first = false; } }
    o << "\n";
    return o.str();
}

bool GameEngine::loadSaveText(const std::string& text, std::string* err) {
    auto kv = parseKV(text);
    if (kv.empty()) {
        if (err) *err = "Empty or malformed save file";
        return false;
    }

    resources_.food          = kvInt(kv, "food",          100);
    resources_.ammunition    = kvInt(kv, "ammunition",    20);
    resources_.medicine      = kvInt(kv, "medicine",      2);
    resources_.health        = kvInt(kv, "health",        85);
    resources_.morale        = kvInt(kv, "morale",        70);
    resources_.horseCondition= kvInt(kv, "horseCondition",100);
    resources_.tradeGoods    = kvInt(kv, "tradeGoods",    6);
    resources_.repJanis      = kvInt(kv, "repJanis",      0);
    resources_.repFriday     = kvInt(kv, "repFriday",     0);
    resources_.repMason      = kvInt(kv, "repMason",      0);
    day_                     = std::max(1, kvInt(kv, "day", 1));
    uint32_t r = (uint32_t)std::strtoul(kv.count("rng") ? kv["rng"].c_str() : "0", nullptr, 10);
    rng_ = r ? r : rng_;

    flags_.clear();
    seen_.clear();
    if (kv.count("flags"))
        for (const std::string& f : splitStr(kv["flags"], ',')) if (!f.empty()) flags_.insert(f);
    if (kv.count("seen"))
        for (const std::string& e : splitStr(kv["seen"], ',')) if (!e.empty()) seen_.insert(e);
    if (kvInt(kv, "flag_moral_contradiction", 0) != 0) flags_.insert("moral_contradiction");

    int ri = kvInt(kv, "routeIndex", 0);
    routeIndex_ = (ri >= 0 && (size_t)ri < route_.size()) ? (size_t)ri : 0;

    gameOverReason_.clear();
    mode_ = GameMode::TRAVEL;
    clampResources();
    if (mode_ == GameMode::GAME_OVER) return true;
    arriveAtStop(false);          // any event at this stop not yet in "seen" still fires
    if (mode_ == GameMode::TRAVEL) footer_ = "Save loaded.";
    return true;
}
