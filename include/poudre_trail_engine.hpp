#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

enum class GameMode {
    TITLE,
    TRAVEL,
    EVENT,
    RESULT,   // outcome of the last choice; "Continue" advances the queue
    TRADE,
    CAMP,     // rest / hunt / load / back
    VICTORY,
    GAME_OVER
};

struct Resources {
    int food = 100, ammunition = 20, medicine = 2, tradeGoods = 6;
    int horseCondition = 100, morale = 70, health = 85;
    int repJanis = 0, repFriday = 0, repMason = 0;
};

struct Location {
    std::string id;
    std::string name;
    std::string summary;
    int milesFromStart = 0;
    bool hasTradingPost = false;
    std::string image;
    int mapX = 50, mapY = 50;   // 0..100, stylised trail-map position
};

struct Choice {
    std::string label;
    std::string resultText;
    int foodDelta = 0;
    int ammunitionDelta = 0;
    int medicineDelta = 0;
    int tradeGoodsDelta = 0;
    int horseConditionDelta = 0;
    int moraleDelta = 0;
    int healthDelta = 0;
    int repJanisDelta = 0;
    int repFridayDelta = 0;
    int repMasonDelta = 0;
    int daysDelta = 0;
    std::string setFlag;        // one or more flags, comma separated
};

struct Event {
    std::string id;
    std::string locationId;
    std::string title;
    std::string body;
    std::string bodyFile;
    std::string imagePath;
    std::string kind;           // story | trail | timed:N
    std::string requiresFlag;   // "a&!b&c|d" = a AND (NOT b) AND (c OR d)
    int minDay = 0;             // timed events fire on arrival once day >= minDay
    std::vector<Choice> choices;
};

struct TradeOffer {
    std::string locationId;
    std::string label;
    int foodDelta = 0;
    int ammunitionDelta = 0;
    int medicineDelta = 0;
    int tradeGoodsDelta = 0;
    int horseDelta = 0;
    int moraleDelta = 0;
    int healthDelta = 0;
    int repJanisDelta = 0;
    int repFridayDelta = 0;
    int repMasonDelta = 0;
    std::string resultText;
};

struct Snapshot {
    GameMode mode = GameMode::TITLE;
    Resources resources;
    int day = 1;
    std::string dateLabel;      // e.g. "May 12, 1864"
    std::string locationId;
    std::string locationName;
    std::string locationSummary;
    int locationIndex = 0;
    int locationCount = 0;
    std::string headline;
    std::string body;
    std::string imagePath;
    std::string footer;
    std::vector<std::string> optionLabels;
    std::string reputationSummaryJanis;
    std::string reputationSummaryFriday;
    std::string reputationSummaryMason;
};

class GameEngine {
public:
    using BodyReader = std::function<std::string(const std::string&)>;

    GameEngine();

    // Replace the world with CSV content. readBody maps a bodyFile path
    // (e.g. "events/janis_store.txt") to its text. On failure the previous
    // world is kept and false is returned.
    bool loadFromText(const std::string& locationsCsv,
                      const std::string& eventsCsv,
                      const std::string& tradesCsv,
                      const BodyReader& readBody,
                      std::string* errorOut = nullptr);

    // Re-load the world compiled into the firmware (world_data.hpp).
    bool loadEmbeddedWorld();

    void seed(uint32_t s) { rng_ = s ? s : 1u; }
    void startGame();
    void travel();                                    // advance to next route stop
    void travelTo(const std::string& nextLocationId); // no-op in VICTORY/GAME_OVER
    void chooseOption(int index);   // EVENT choice, TRADE offer, RESULT continue, CAMP action
    void openTrade();
    void openCamp();
    void closeCamp();

    Snapshot snapshot() const;
    const std::vector<Location>& route() const { return locations_; }
    size_t routeIndex() const { return routeIndex_; }
    bool hasFlag(const std::string& f) const;

    std::string serializeSave() const;
    bool loadSaveText(const std::string& text, std::string* errorOut = nullptr);

private:
    void buildMinimalWorld();
    void arriveAtStop(bool rollTrailEvent);
    void advanceQueue();
    void finishJourney();
    void advanceDays(int n);
    void applyDeltas(int food, int ammo, int med, int trade, int horse, int morale,
                     int health, int rj, int rf, int rm);
    bool flagExpr(const std::string& expr) const;
    bool eventEligible(const Event& ev) const;
    const Event* findEvent(const std::string& id) const;
    uint32_t nextRand();
    void evaluateReputation();
    void clampResources();
    std::string repLabel(int value, const std::string& npc) const;
    std::string dateLabel() const;
    std::string deltaSummary(const Resources& before) const;
    const Location* findLocation(const std::string& id) const;

    GameMode mode_ = GameMode::TITLE;
    Resources resources_;
    std::vector<Location> locations_;
    std::vector<std::string> route_;
    size_t routeIndex_ = 0;
    int day_ = 1;
    std::vector<Event> events_;
    std::map<std::string, std::vector<TradeOffer>> trades_;
    std::set<std::string> flags_;
    std::set<std::string> seen_;
    std::deque<std::string> pending_;
    const Event* currentEvent_ = nullptr;
    std::vector<TradeOffer> currentTrades_;
    std::string footer_;
    std::string resultTitle_, resultText_, resultImage_;
    std::string gameOverReason_;
    std::string endingTitle_, endingBody_;
    uint32_t rng_ = 0x9E3779B9u;
};
