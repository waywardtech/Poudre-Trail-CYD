#include "poudre_trail_engine.hpp"
#include <algorithm>
#include <cctype>
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

// CSV column layout for choices (13 columns each, starting at offset):
// 0:label 1:food 2:ammo 3:med 4:trade 5:horse 6:morale 7:health
// 8:repJanis 9:repFriday 10:repMason 11:setFlag 12:result
static const int CHOICE_COLS = 13;

static Choice parseChoice(const std::vector<std::string>& row, int off) {
    Choice c;
    if ((int)row.size() < off + CHOICE_COLS) return c;
    c.label           = row[off+0];
    c.foodDelta       = csvInt(row[off+1]);
    c.ammunitionDelta = csvInt(row[off+2]);
    c.medicineDelta   = csvInt(row[off+3]);
    c.tradeGoodsDelta = csvInt(row[off+4]);
    c.horseConditionDelta = csvInt(row[off+5]);
    c.moraleDelta     = csvInt(row[off+6]);
    c.healthDelta     = csvInt(row[off+7]);
    c.repJanisDelta   = csvInt(row[off+8]);
    c.repFridayDelta  = csvInt(row[off+9]);
    c.repMasonDelta   = csvInt(row[off+10]);
    c.setFlag         = row[off+11];
    c.resultText      = row[off+12];
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

GameEngine::GameEngine() { buildFallbackWorld(); }

bool GameEngine::loadFromText(const std::string& locCsv,
                               const std::string& evCsv,
                               const std::string& trCsv,
                               const std::vector<std::pair<std::string,std::string>>& bodyFiles,
                               std::string* err) {
    auto locRows = parseCsv(locCsv);
    auto evRows  = parseCsv(evCsv);
    auto trRows  = parseCsv(trCsv);

    if (locRows.size() < 2 || evRows.size() < 2) {
        if (err) *err = "CSV missing headers or data";
        return false;
    }

    locations_.clear();
    events_.clear();
    trades_.clear();
    locationNames_.clear();
    route_.clear();
    bodyFiles_ = bodyFiles;

    // Locations (header row 0, data from row 1)
    // id,name,summary,milesFromStart,hasTradingPost,image
    for (size_t i = 1; i < locRows.size(); ++i) {
        const auto& r = locRows[i];
        if (r.size() < 6) continue;
        Location loc;
        loc.id            = r[0];
        loc.name          = r[1];
        loc.summary       = r[2];
        loc.milesFromStart= csvInt(r[3]);
        loc.hasTradingPost= csvBool(r[4]);
        loc.image         = r[5];
        locations_.push_back(loc);
        locationNames_[loc.id] = loc.name;
        route_.push_back(loc.id);
    }

    // Events: id,locationId,title,bodyFile,image,kind,requiresFlag,
    //         c1(13 cols), c2(13 cols), c3(13 cols)
    for (size_t i = 1; i < evRows.size(); ++i) {
        const auto& r = evRows[i];
        if (r.size() < 7) continue;
        Event ev;
        ev.id          = r[0];
        ev.locationId  = r[1];
        ev.title       = r[2];
        ev.bodyFile    = r[3];
        ev.imagePath   = r[4];
        // r[5] = kind (ignored at runtime, used for editor filtering)
        ev.requiresFlag= r[6];
        for (int off = 7; off + CHOICE_COLS <= (int)r.size(); off += CHOICE_COLS) {
            Choice c = parseChoice(r, off);
            if (!trimStr(c.label).empty()) ev.choices.push_back(c);
        }
        ev.body = bodyTextFor(ev.bodyFile);
        if (ev.body.empty()) ev.body = ev.title;
        events_.push_back(ev);
    }

    // Trades
    for (size_t i = 1; i < trRows.size(); ++i) {
        TradeOffer t = parseTrade(trRows[i]);
        if (!trimStr(t.label).empty())
            trades_[t.locationId].push_back(t);
    }

    if (locations_.empty() || events_.empty()) {
        if (err) *err = "No usable locations or events after parse";
        buildFallbackWorld();
        return false;
    }
    return true;
}

std::string GameEngine::bodyTextFor(const std::string& bodyFile) const {
    for (const auto& p : bodyFiles_)
        if (p.first == bodyFile) return p.second;
    return {};
}

const Location* GameEngine::findLocation(const std::string& id) const {
    for (const auto& loc : locations_)
        if (loc.id == id) return &loc;
    return nullptr;
}

static Location makeLocation(const std::string& id, const std::string& name,
                              const std::string& summary, int miles, bool trade,
                              const std::string& img) {
    Location l; l.id=id; l.name=name; l.summary=summary;
    l.milesFromStart=miles; l.hasTradingPost=trade; l.image=img; return l;
}
static Choice makeChoice(const std::string& lbl, const std::string& res,
                          int food, int ammo, int med, int trd, int horse, int mor, int hp,
                          int rj, int rf, int rm, const std::string& flag) {
    Choice c; c.label=lbl; c.resultText=res;
    c.foodDelta=food; c.ammunitionDelta=ammo; c.medicineDelta=med;
    c.tradeGoodsDelta=trd; c.horseConditionDelta=horse; c.moraleDelta=mor; c.healthDelta=hp;
    c.repJanisDelta=rj; c.repFridayDelta=rf; c.repMasonDelta=rm; c.setFlag=flag; return c;
}
static TradeOffer makeTrade(const std::string& loc, const std::string& lbl,
                             int food, int ammo, int med, int trd, int horse, int mor, int hp,
                             int rj, int rf, int rm, const std::string& res) {
    TradeOffer t; t.locationId=loc; t.label=lbl;
    t.foodDelta=food; t.ammunitionDelta=ammo; t.medicineDelta=med;
    t.tradeGoodsDelta=trd; t.horseDelta=horse; t.moraleDelta=mor; t.healthDelta=hp;
    t.repJanisDelta=rj; t.repFridayDelta=rf; t.repMasonDelta=rm; t.resultText=res; return t;
}

void GameEngine::buildFallbackWorld() {
    locations_.clear();
    locations_.push_back(makeLocation("laporte",      "Laporte",      "Janis's settlement at the door of the valley.", 0,  true,  "art/laporte.bmp"));
    locations_.push_back(makeLocation("council_tree", "Council Tree", "A place of gathering, memory, and warning.",    8,  false, "art/council_tree.bmp"));
    locations_.push_back(makeLocation("virginia_dale","Virginia Dale","The home station between the mountain and the plain.", 18, true, "art/stage_road.bmp"));
    locations_.push_back(makeLocation("mason_claim",  "Mason's Claim","Downstream ambition hardening into institutions.", 30, true,  "art/mason_claim.bmp"));
    locations_.push_back(makeLocation("fort_collins", "Fort Collins", "The new order gathers on higher ground.",        42, false, "art/fort_collins.bmp"));

    locationNames_.clear();
    route_.clear();
    for (const auto& loc : locations_) {
        locationNames_[loc.id] = loc.name;
        route_.push_back(loc.id);
    }

    events_.clear();
    trades_.clear();
    flags_.clear();

    Event janis;
    janis.id = "janis_store"; janis.locationId = "laporte";
    janis.title = "Janis at the Store";
    janis.imagePath = "art/janis.bmp";
    janis.body = "The store smells of harness leather, coffee, wet wool, flour dust, and river mud. Antoine Janis speaks like a man who knows this valley had owners, obligations, and names before the stage line decided it mattered.";
    janis.choices.push_back(makeChoice("Buy flour and hear the safer road",
        "Janis marks the line that holds when the river rises. Your household leaves better supplied.",
        +10,0,0,-1,0,0,0, +4,0,0, "met_janis"));
    janis.choices.push_back(makeChoice("Ask what the valley asks of travelers",
        "He answers carefully. What you gain is not a shortcut but a standard.",
        0,0,0,0,0,+3,0, +2,+1,0, "met_janis"));
    events_.push_back(janis);

    Event friday;
    friday.id = "friday_counsel"; friday.locationId = "council_tree";
    friday.title = "Chief Friday's Counsel";
    friday.imagePath = "art/chief_friday.bmp";
    friday.body = "Friday does not offer folklore. He offers calibration: how to move without mistaking speed for wisdom, how to hear warning before it becomes consequence, how to remain visible without becoming reckless.";
    friday.choices.push_back(makeChoice("Listen and slow your pace",
        "You leave with sharper eyes and a quieter mind.",
        0,0,0,0,0,+4,0, 0,+5,0, "heard_friday"));
    friday.choices.push_back(makeChoice("Offer tobacco and exchange news",
        "Respect offered carefully and received in kind.",
        0,0,0,-1,0,+2,0, +2,+3,0, "heard_friday"));
    friday.choices.push_back(makeChoice("Press on with the stage timetable",
        "You keep to the company clock. It is not the only clock in this country.",
        0,0,0,0,0,-2,0, 0,-3,+1, ""));
    events_.push_back(friday);

    Event virginia;
    virginia.id = "virginia_dale_station"; virginia.locationId = "virginia_dale";
    virginia.title = "Virginia at the Home Station";
    virginia.imagePath = "art/stage_road.bmp";
    virginia.body = "At Virginia Dale the station runs on labor, memory, and nerves held in check. Virginia Slade has no interest in mythmaking.";
    virginia.choices.push_back(makeChoice("Offer help with the horses",
        "She accepts the labor. The station remembers useful hands.",
        0,0,0,0,+4,+2,0, 0,+1,0, "helped_virginia"));
    virginia.choices.push_back(makeChoice("Ask after Jack Slade's latest ride",
        "Her look tells you the question is cheaper than the answer.",
        0,0,0,0,0,-2,0, 0,0,0, ""));
    events_.push_back(virginia);

    Event mason;
    mason.id = "mason_reveal"; mason.locationId = "mason_claim";
    mason.title = "Mason Makes His Choice";
    mason.imagePath = "art/mason_claim.bmp";
    mason.body = "Mason knows exactly what Laporte is and exactly what is replacing it. He does not need to hate the older order to leave it behind.";
    mason.choices.push_back(makeChoice("Trade with him anyway",
        "You gain provisions by stepping closer to the new order. It pays promptly.",
        +8,0,0,-1,0,0,0, 0,-1,+5, "traded_with_mason"));
    mason.choices.push_back(makeChoice("Keep your distance",
        "You leave poorer in goods and clearer in mind.",
        -4,0,0,0,0,+1,0, 0,+1,-3, ""));
    events_.push_back(mason);

    trades_["laporte"].push_back(makeTrade("laporte", "Janis's sack flour (+10 food)", +10,0,0,-1,0,0,0, +2,0,0, "Your packs are heavier, but your household is steadier."));
    trades_["virginia_dale"].push_back(makeTrade("virginia_dale", "Buy cartridges (+8 ammo)", 0,+8,0,-1,0,0,0, 0,0,0, "Stage traffic always turns risk into a price."));
    trades_["mason_claim"].push_back(makeTrade("mason_claim", "Mason's horse feed (+8 horse)", 0,0,0,-1,+8,0,0, 0,-1,+2, "The horses recover. Mason's men remember the bargain."));

    footer_ = "Tap to begin.";
}

void GameEngine::startGame() {
    resources_ = Resources{};
    flags_.clear();
    routeIndex_ = 0;
    day_ = 1;
    travelCount_ = 0;
    currentEvent_ = nullptr;
    currentTrades_.clear();
    footer_ = "La Porte, 1861.";
    // Janis event fires immediately at start
    mode_ = GameMode::EVENT;
    queueEventForCurrentStop();
}

const Event* GameEngine::findEligibleEventForLocation(const std::string& locId) const {
    for (const auto& ev : events_) {
        if (ev.locationId != locId) continue;
        if (!ev.requiresFlag.empty()) {
            auto it = flags_.find(ev.requiresFlag);
            if (it == flags_.end() || !it->second) continue;
        }
        if (resources_.repJanis  < ev.minRepJanis)  continue;
        if (resources_.repFriday < ev.minRepFriday)  continue;
        if (resources_.repMason  < ev.minRepMason)   continue;
        return &ev;
    }
    return nullptr;
}

void GameEngine::queueEventForCurrentStop() {
    const std::string& loc = route_[routeIndex_];
    currentEvent_ = findEligibleEventForLocation(loc);
    if (currentEvent_) {
        mode_ = GameMode::EVENT;
        footer_ = "An encounter unfolds.";
    } else if (mode_ != GameMode::VICTORY && mode_ != GameMode::GAME_OVER) {
        mode_ = GameMode::TRAVEL;
        footer_ = "No encounter here.";
    }
}

// travel() advances one step along the route; safe to call from TRAVEL mode button.
void GameEngine::travel() {
    if (mode_ != GameMode::TRAVEL) return;
    if (routeIndex_ + 1 < route_.size())
        travelTo(route_[routeIndex_ + 1]);
}

// travelTo: no-op in VICTORY or GAME_OVER (mode guard from repfix).
// Food drain: -12 (increased from art_save's -8; see changelog).
void GameEngine::travelTo(const std::string& nextLocId) {
    if (mode_ == GameMode::VICTORY || mode_ == GameMode::GAME_OVER) return;
    auto it = std::find(route_.begin(), route_.end(), nextLocId);
    if (it == route_.end()) return;

    routeIndex_ = (size_t)std::distance(route_.begin(), it);
    day_ += 3;
    ++travelCount_;
    resources_.food          -= 12;
    resources_.horseCondition -= 6;
    resources_.morale        -= 1;
    clampResources();
    if (mode_ == GameMode::GAME_OVER) return;

    if (routeIndex_ == route_.size() - 1) {
        mode_ = GameMode::VICTORY;
        footer_ = flags_["moral_contradiction"]
            ? "You reached Fort Collins carrying provisions, memory, and contradiction. Survival did not keep your hands entirely clean."
            : "You reached Fort Collins, but the valley's balance has shifted around you.";
        return;
    }

    queueEventForCurrentStop();
}

void GameEngine::chooseOption(int index) {
    if (mode_ == GameMode::EVENT) {
        if (!currentEvent_ || index < 0 || (size_t)index >= currentEvent_->choices.size()) return;
        const Choice& c = currentEvent_->choices[index];
        resources_.food          += c.foodDelta;
        resources_.ammunition    += c.ammunitionDelta;
        resources_.medicine      += c.medicineDelta;
        resources_.tradeGoods    += c.tradeGoodsDelta;
        resources_.horseCondition+= c.horseConditionDelta;
        resources_.morale        += c.moraleDelta;
        resources_.health        += c.healthDelta;
        resources_.repJanis      += c.repJanisDelta;
        resources_.repFriday     += c.repFridayDelta;
        resources_.repMason      += c.repMasonDelta;
        if (!c.setFlag.empty()) flags_[c.setFlag] = true;
        footer_ = c.resultText.empty() ? "Choose your next move." : c.resultText;
        currentEvent_ = nullptr;
        clampResources();
        evaluateReputation();
        if (mode_ != GameMode::GAME_OVER) mode_ = GameMode::TRAVEL;
    } else if (mode_ == GameMode::TRADE) {
        if (index < 0 || (size_t)index >= currentTrades_.size()) {
            // "Leave" button
            footer_ = "You leave the trading post.";
            mode_ = GameMode::TRAVEL;
            return;
        }
        const TradeOffer& t = currentTrades_[index];
        resources_.food          += t.foodDelta;
        resources_.ammunition    += t.ammunitionDelta;
        resources_.medicine      += t.medicineDelta;
        resources_.tradeGoods    += t.tradeGoodsDelta;
        resources_.horseCondition+= t.horseDelta;
        resources_.morale        += t.moraleDelta;
        resources_.health        += t.healthDelta;
        resources_.repJanis      += t.repJanisDelta;
        resources_.repFriday     += t.repFridayDelta;
        resources_.repMason      += t.repMasonDelta;
        footer_ = t.resultText.empty() ? "Trade completed." : t.resultText;
        clampResources();
        evaluateReputation();
        if (mode_ != GameMode::GAME_OVER) mode_ = GameMode::TRAVEL;
    }
}

void GameEngine::openTrade() {
    currentTrades_.clear();
    auto it = trades_.find(route_[routeIndex_]);
    if (it == trades_.end() || it->second.empty()) {
        footer_ = "No trade here.";
        mode_ = GameMode::TRAVEL;
        return;
    }
    currentTrades_ = it->second;
    mode_ = GameMode::TRADE;
    footer_ = "Choose a trade.";
}

// moral_contradiction: repMason >= 5 AND repFriday >= 5 (lowered threshold from earlier builds)
void GameEngine::evaluateReputation() {
    if (resources_.repMason >= 5 && resources_.repFriday >= 5)
        flags_["moral_contradiction"] = true;
}

void GameEngine::clampResources() {
    resources_.food          = std::max(0, resources_.food);
    resources_.ammunition    = std::max(0, resources_.ammunition);
    resources_.medicine      = std::max(0, resources_.medicine);
    resources_.tradeGoods    = std::max(0, resources_.tradeGoods);
    resources_.horseCondition= std::max(0, std::min(100, resources_.horseCondition));
    resources_.morale        = std::max(0, std::min(100, resources_.morale));
    resources_.health        = std::max(0, std::min(100, resources_.health));
    if (resources_.food == 0 || resources_.health == 0) {
        mode_ = GameMode::GAME_OVER;
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

Snapshot GameEngine::snapshot() const {
    Snapshot s;
    s.mode = mode_;
    s.resources = resources_;
    s.day = day_;

    if (!route_.empty()) {
        s.locationId = route_[routeIndex_];
        auto nm = locationNames_.find(s.locationId);
        s.locationName = (nm != locationNames_.end()) ? nm->second : s.locationId;
        const Location* loc = findLocation(s.locationId);
        if (loc) {
            s.locationSummary = loc->summary;
            s.imagePath = loc->image;
        }
    }

    s.footer = footer_;
    s.reputationSummaryJanis  = repLabel(resources_.repJanis,  "Janis");
    s.reputationSummaryFriday = repLabel(resources_.repFriday, "Friday");
    s.reputationSummaryMason  = repLabel(resources_.repMason,  "Mason");

    if (mode_ == GameMode::TITLE) {
        s.headline = "Poudre Trail";
        s.body = "Travel the Cache la Poudre valley, 1861. Trade carefully, choose wisely.";
        s.optionLabels = {"Start Journey", "Load Save"};
        return s;
    }

    if (mode_ == GameMode::TRAVEL) {
        s.headline = s.locationName;
        s.body = s.locationSummary;
        s.optionLabels = {"Travel onward", "Trade / barter", "Save game", "Load game"};
        return s;
    }

    if (mode_ == GameMode::EVENT && currentEvent_) {
        s.headline = currentEvent_->title;
        s.body = currentEvent_->body;
        if (!currentEvent_->imagePath.empty()) s.imagePath = currentEvent_->imagePath;
        for (const auto& c : currentEvent_->choices) s.optionLabels.push_back(c.label);
        return s;
    }

    if (mode_ == GameMode::TRADE) {
        s.headline = "Trading Post";
        s.body = "Supplies and favors are priced differently in each place. Spend carefully.";
        for (const auto& t : currentTrades_) s.optionLabels.push_back(t.label);
        s.optionLabels.push_back("Leave trading post");
        return s;
    }

    if (mode_ == GameMode::GAME_OVER) {
        s.headline = "Journey Lost";
        s.body = "Food or health failed before the road was done.";
        s.optionLabels = {"Restart", "Load Save"};
        return s;
    }

    // VICTORY
    s.headline = "Fort Collins";
    s.body = footer_;
    s.optionLabels = {"Restart", "Load Save"};
    return s;
}

std::string GameEngine::serializeSave() const {
    std::ostringstream o;
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
    o << "flag_moral_contradiction=" << (flags_.count("moral_contradiction") && flags_.at("moral_contradiction") ? 1 : 0) << "\n";
    return o.str();
}

bool GameEngine::loadSaveText(const std::string& text, std::string* err) {
    auto kv = parseKV(text);
    if (kv.empty()) {
        if (err) *err = "Empty or malformed save file";
        return false;
    }

    resources_.food          = kvInt(kv, "food",          120);
    resources_.ammunition    = kvInt(kv, "ammunition",    30);
    resources_.medicine      = kvInt(kv, "medicine",      3);
    resources_.health        = kvInt(kv, "health",        85);
    resources_.morale        = kvInt(kv, "morale",        70);
    resources_.horseCondition= kvInt(kv, "horseCondition",100);
    resources_.tradeGoods    = kvInt(kv, "tradeGoods",    6);
    resources_.repJanis      = kvInt(kv, "repJanis",      0);
    resources_.repFriday     = kvInt(kv, "repFriday",     0);
    resources_.repMason      = kvInt(kv, "repMason",      0);
    day_                     = kvInt(kv, "day",           1);

    flags_["moral_contradiction"] = kvInt(kv, "flag_moral_contradiction", 0) != 0;

    int ri = kvInt(kv, "routeIndex", 0);
    routeIndex_ = (ri >= 0 && (size_t)ri < route_.size()) ? (size_t)ri : 0;

    if (routeIndex_ == route_.size() - 1) {
        mode_ = GameMode::VICTORY;
        footer_ = flags_["moral_contradiction"]
            ? "You reached Fort Collins carrying provisions, memory, and contradiction."
            : "You reached Fort Collins, but the valley's balance has shifted around you.";
    } else {
        queueEventForCurrentStop();
    }
    clampResources();
    return true;
}
