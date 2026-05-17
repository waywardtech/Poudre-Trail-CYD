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
    switch (routeIndex_) {
        case 0: // La Porte: Janis's store
            metJanis_ = true;
            if (index == 0) { r_.food += 20; r_.tradeGoods -= 1; r_.repJanis += 2; footer_ = "Janis adds salt pork to your packs."; }
            else            { r_.morale -= 4; r_.repJanis -= 1; footer_ = "You move on without trade."; }
            break;
        case 1: // Council Tree: Friday's counsel
            heardFriday_ = true;
            if (index == 0) { r_.morale += 6; r_.repFriday += 2; footer_ = "Friday's counsel steadies the party."; }
            else            { r_.repFriday -= 2; r_.morale -= 2; footer_ = "You waved off the elder."; }
            break;
        case 2: // Virginia Dale: Slade's home
            if (index == 0) { r_.medicine += 1; r_.repMason -= 1; footer_ = "Virginia spares medicine but warns you."; }
            else            { r_.ammunition += 4; r_.repMason += 1; footer_ = "Slade slides ammunition across the table."; }
            break;
        case 3: // Mason's Claim: reveal
            if (index == 0) { r_.tradeGoods += 3; r_.repMason += 2; footer_ = "Mason cuts you in on the claim."; }
            else            { r_.morale += 2; r_.repMason -= 3; r_.repFriday += 1; footer_ = "You refuse Mason's offer."; }
            break;
        default:
            footer_ = "You press on toward Fort Collins.";
            break;
    }
    evaluateReputation();
    clampResources();
    if (mode_ == GameMode::EVENT) mode_ = GameMode::TRAVEL;
}

void GameEngine::openTrade() {
    if (mode_ == GameMode::TRAVEL) {
        mode_ = GameMode::TRADE;
        footer_ = "Trade with locals.";
    }
}

void GameEngine::applyTrade(int index) {
    if (mode_ != GameMode::TRADE) return;

    switch (index) {
        case 0:
            if (r_.tradeGoods >= 2) {
                r_.food += 10;
                r_.tradeGoods -= 2;
                footer_ = "You trade goods for provisions.";
            } else {
                footer_ = "Not enough trade goods.";
            }
            break;
        case 1:
            if (r_.tradeGoods >= 1) {
                r_.ammunition += 5;
                r_.tradeGoods -= 1;
                footer_ = "You trade goods for ammunition.";
            } else {
                footer_ = "Not enough trade goods.";
            }
            break;
        default:
            footer_ = "No trade chosen.";
            break;
    }

    mode_ = GameMode::TRAVEL;
    clampResources();
}

void GameEngine::backFromTrade() {
    if (mode_ == GameMode::TRADE) {
        mode_ = GameMode::TRAVEL;
        footer_ = "You step away from the market.";
    }
}

Snapshot GameEngine::snapshot() const {
    Snapshot s;
    s.mode = mode_;
    s.resources = r_;
    int routeClamped = routeIndex_;
    if (routeClamped < 0) routeClamped = 0;
    if (routeClamped > 4) routeClamped = 4;
    s.locationId = String(routeClamped);
    s.locationName = ROUTE_NAMES[routeClamped];
    s.footer = footer_;
    s.currentTone = EmotionTone::STEADY;
    switch (routeClamped) {
        case 0:
            s.eventId = "janis_store";
            s.eventTitle = "Janis's Store";
            s.eventBody = "Janis hauls a crate to the counter and meets your eye. 'Salt pork or rolling tobacco. Either way, your packs leave heavier than they came in.'";
            s.visibleChoices[0] = "Trade goods";
            s.visibleChoices[1] = "Move on";
            s.visibleChoiceCount = 2;
            break;
        case 1:
            s.eventId = "friday_counsel";
            s.eventTitle = "Friday's Counsel";
            s.eventBody = "Under the council tree, Friday speaks slow. 'The valley is watching. Listen before the next ridge or the next ridge will listen for you.'";
            s.visibleChoices[0] = "Listen";
            s.visibleChoices[1] = "Wave it off";
            s.visibleChoiceCount = 2;
            break;
        case 2:
            s.eventId = "virginia_slade_home";
            s.eventTitle = "The Slade Home";
            s.eventBody = "Virginia sets a tin cup down. Behind her, Slade leans on his rifle. 'You can take medicine from me, or powder from him. Both have a price.'";
            s.visibleChoices[0] = "Take medicine";
            s.visibleChoices[1] = "Take powder";
            s.visibleChoiceCount = 2;
            break;
        case 3:
            s.eventId = "mason_reveal";
            s.eventTitle = "Mason's Claim";
            s.eventBody = "Mason unfolds a deed and a hard smile. 'Sign on, and the claim feeds us both. Walk away, and Friday's people sleep easier tonight.'";
            s.visibleChoices[0] = "Sign on";
            s.visibleChoices[1] = "Walk away";
            s.visibleChoiceCount = 2;
            break;
        default:
            s.eventId = "fort_collins";
            s.eventTitle = "Fort Collins";
            s.eventBody = "The palisade lifts on the horizon. The valley quiets behind you.";
            s.visibleChoices[0] = "Continue";
            s.visibleChoiceCount = 1;
            break;
    }
    s.reputationSummaryJanis = repLabel(r_.repJanis, "Janis");
    s.reputationSummaryFriday = repLabel(r_.repFriday, "Friday");
    s.reputationSummaryMason = repLabel(r_.repMason, "Mason");

    if (mode_ == GameMode::TRADE) {
        s.tradeChoices[0] = "Buy food";
        s.tradeChoices[1] = "Buy ammo";
        s.tradeChoiceCount = 2;
    }

    return s;
}

String GameEngine::serializeSave() const {
    String s;
    s += "version=1\n";
    s += "mode=" + String((int)mode_) + "\n";
    s += "route=" + String(routeIndex_) + "\n";
    s += "food=" + String(r_.food) + "\n";
    s += "ammo=" + String(r_.ammunition) + "\n";
    s += "med=" + String(r_.medicine) + "\n";
    s += "trade=" + String(r_.tradeGoods) + "\n";
    s += "horse=" + String(r_.horseCondition) + "\n";
    s += "morale=" + String(r_.morale) + "\n";
    s += "health=" + String(r_.health) + "\n";
    s += "repJanis=" + String(r_.repJanis) + "\n";
    s += "repFriday=" + String(r_.repFriday) + "\n";
    s += "repMason=" + String(r_.repMason) + "\n";
    s += "metJanis=" + String(metJanis_ ? 1 : 0) + "\n";
    s += "heardFriday=" + String(heardFriday_ ? 1 : 0) + "\n";
    s += "moralContradiction=" + String(moralContradiction_ ? 1 : 0) + "\n";
    return s;
}

bool GameEngine::loadSaveText(const String& text) {
    if (text.length() == 0) return false;
    Resources r;
    GameMode mode = GameMode::TRAVEL;
    int route = 0;
    bool met = false, heard = false, moral = false;
    int start = 0;
    while (start < text.length()) {
        int nl = text.indexOf('\n', start);
        String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
        start = (nl < 0) ? text.length() : nl + 1;
        line.trim();
        if (line.length() == 0) continue;
        int eq = line.indexOf('=');
        if (eq <= 0) continue;
        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);
        if      (key == "mode")               mode = (GameMode)val.toInt();
        else if (key == "route")              route = val.toInt();
        else if (key == "food")               r.food = val.toInt();
        else if (key == "ammo")               r.ammunition = val.toInt();
        else if (key == "med")                r.medicine = val.toInt();
        else if (key == "trade")              r.tradeGoods = val.toInt();
        else if (key == "horse")              r.horseCondition = val.toInt();
        else if (key == "morale")             r.morale = val.toInt();
        else if (key == "health")             r.health = val.toInt();
        else if (key == "repJanis")           r.repJanis = val.toInt();
        else if (key == "repFriday")          r.repFriday = val.toInt();
        else if (key == "repMason")           r.repMason = val.toInt();
        else if (key == "metJanis")           met = val.toInt() != 0;
        else if (key == "heardFriday")        heard = val.toInt() != 0;
        else if (key == "moralContradiction") moral = val.toInt() != 0;
    }
    r_ = r;
    mode_ = mode;
    routeIndex_ = route;
    metJanis_ = met;
    heardFriday_ = heard;
    moralContradiction_ = moral;
    footer_ = "Save loaded.";
    return true;
}
