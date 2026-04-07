#pragma once
#include <Arduino.h>

enum class GameMode { TITLE, TRAVEL, EVENT, TRADE, VICTORY, GAME_OVER };
enum class EmotionTone { STEADY, WARY, OPEN, PRESSURED, CONTRADICTED };

struct Resources {
    int food = 120;
    int ammunition = 30;
    int medicine = 3;
    int tradeGoods = 6;
    int horseCondition = 100;
    int morale = 70;
    int health = 85;
    int repJanis = 0;
    int repFriday = 0;
    int repMason = 0;
};

struct Snapshot {
    GameMode mode = GameMode::TITLE;
    Resources resources;
    String locationId;
    String locationName;
    String footer;
    String eventId;
    String eventTitle;
    String eventBody;
    String eventImagePath;
    EmotionTone currentTone = EmotionTone::STEADY;
    String visibleChoices[3];
    int visibleChoiceCount = 0;
    String tradeChoices[3];
    int tradeChoiceCount = 0;
    String reputationSummaryJanis;
    String reputationSummaryFriday;
    String reputationSummaryMason;
};

class GameEngine {
public:
    GameEngine();
    void startGame();
    void restartToTitle();
    void continueTravel();
    void chooseEventOption(int index);
    void openTrade();
    void applyTrade(int index);
    void backFromTrade();
    Snapshot snapshot() const;
    String serializeSave() const;
    bool loadSaveText(const String& text);
private:
    Resources r_;
    GameMode mode_ = GameMode::TITLE;
    int routeIndex_ = 0;
    bool metJanis_ = false;
    bool heardFriday_ = false;
    bool moralContradiction_ = false;
    String footer_;
    void queueEvent();
    void clampResources();
    void evaluateReputation();
    String repLabel(int value, const String& npc) const;
};
