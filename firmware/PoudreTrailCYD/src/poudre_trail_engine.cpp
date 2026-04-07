#include "poudre_trail_engine.hpp"

static const char* ROUTE_NAMES[] = {"La Porte","Council Tree","Virginia Dale","Mason's Claim","Fort Collins"};

GameEngine::GameEngine() { restartToTitle(); }

void GameEngine::restartToTitle() {
    r_ = {};
    mode_ = GameMode::TITLE;
    routeIndex_ = 0;
    metJanis_ = heardFriday_ = moralContradiction_ = false;
    footer_ = "Tap New Game.";
}

String GameEngine::repLabel(int v, const String& npc) const {
    if (v >= 5) return "Open";
    if (v >= 0) return "Neutral";
    return "Wary";
}

void GameEngine::startGame() {
    r_ = {};
    routeIndex_ = 0;
    footer_ = "La Porte, 1861.";
    mode_ = GameMode::EVENT;
    queueEvent();
}

void GameEngine::queueEvent() {
    if (routeIndex_ < 4) {
        mode_ = GameMode::EVENT;
        footer_ = "An encounter unfolds.";
    } else {
        mode_ = GameMode::VICTORY;
    }
}

void GameEngine::clampResources() {
    if (r_.food <= 0 || r_.health <= 0) {
        mode_ = GameMode::GAME_OVER;
        footer_ = "You cannot continue.";
    }
}

void GameEngine::evaluateReputation() {
    if (r_.repMason > 3 && r_.repFriday > 3) moralContradiction_ = true;
}

void GameEngine::continueTravel() {
    if (mode_ != GameMode::TRAVEL) return;
    routeIndex_++;
    r_.food -= 10;
    clampResources();
    if (routeIndex_ == 4) {
        mode_ = GameMode::VICTORY;
        footer_ = "You reached Fort Collins.";
        return;
    }
    queueEvent();
}

void GameEngine::chooseEventOption(int index) {
    if (mode_ != GameMode::EVENT) return;
    r_.food += 5;
    r_.repJanis += 1;
    footer_ = "You made a choice.";
    mode_ = GameMode::TRAVEL;
}

void GameEngine::openTrade() { if (mode_ == GameMode::TRAVEL) mode_ = GameMode::TRADE; }
void GameEngine::applyTrade(int index) { mode_ = GameMode::TRAVEL; }
void GameEngine::backFromTrade() { mode_ = GameMode::TRAVEL; }

Snapshot GameEngine::snapshot() const {
    Snapshot s;
    s.mode = mode_;
    s.resources = r_;
    s.locationName = ROUTE_NAMES[routeIndex_];
    s.footer = footer_;
    s.eventTitle = "Encounter";
    s.eventBody = "Something happens.";
    s.visibleChoices[0] = "Continue";
    s.visibleChoiceCount = 1;
    return s;
}

String GameEngine::serializeSave() const { return ""; }
bool GameEngine::loadSaveText(const String& text) { return true; }
