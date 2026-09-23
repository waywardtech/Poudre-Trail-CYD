#pragma once
#include <string>
#include <vector>
#include <map>

enum class GameMode {
    TITLE,
    TRAVEL,
    EVENT,
    TRADE,
    VICTORY,
    GAME_OVER
};

struct Resources {
    int food = 120;
    int ammunition = 30;
    int medicine = 3;
    int tradeGoods = 6;
    int horseCondition = 100;
    int morale = 70;
    int health = 85;
    // split reputation (was single int in earlier builds)
    int repJanis = 0;
    int repFriday = 0;
    int repMason = 0;
};

struct Location {
    std::string id;
    std::string name;
    std::string summary;
    int milesFromStart = 0;
    bool hasTradingPost = false;
    std::string image;
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
    std::string setFlag;
};

struct Event {
    std::string id;
    std::string locationId;
    std::string title;
    std::string body;
    std::string bodyFile;
    std::string imagePath;
    std::string requiresFlag;
    int minRepJanis = -999;
    int minRepFriday = -999;
    int minRepMason = -999;
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
    std::string locationId;
    std::string locationName;
    std::string locationSummary;
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
    GameEngine();

    // Load game world from CSV text; falls back to built-in world on failure.
    // bodyFiles: pairs of (relative path, file contents) for event body text.
    bool loadFromText(const std::string& locationsCsv,
                      const std::string& eventsCsv,
                      const std::string& tradesCsv,
                      const std::vector<std::pair<std::string, std::string>>& bodyFiles,
                      std::string* errorOut = nullptr);

    void startGame();
    void travel();                                    // advance to next route stop
    void travelTo(const std::string& nextLocationId); // direct route jump (no-op in VICTORY/GAME_OVER)
    void chooseOption(int index);                     // select event choice or trade
    void openTrade();

    Snapshot snapshot() const;
    std::string serializeSave() const;
    bool loadSaveText(const std::string& text, std::string* errorOut = nullptr);

private:
    void buildFallbackWorld();
    void queueEventForCurrentStop();
    void evaluateReputation();
    void clampResources();
    std::string repLabel(int value, const std::string& npc) const;
    const Event* findEligibleEventForLocation(const std::string& locId) const;
    std::string bodyTextFor(const std::string& bodyFile) const;
    const Location* findLocation(const std::string& id) const;

    GameMode mode_ = GameMode::TITLE;
    Resources resources_;
    std::vector<Location> locations_;
    std::map<std::string, std::string> locationNames_;
    std::vector<std::string> route_;
    size_t routeIndex_ = 0;
    int day_ = 1;
    int travelCount_ = 0;
    std::vector<Event> events_;
    std::map<std::string, std::vector<TradeOffer>> trades_;
    std::map<std::string, bool> flags_;
    const Event* currentEvent_ = nullptr;
    std::vector<TradeOffer> currentTrades_;
    std::string footer_;
    std::vector<std::pair<std::string, std::string>> bodyFiles_;
};
