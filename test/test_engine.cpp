// Host-side test harness for poudre_trail_engine.
// Build and run: cd test && make run
// No Arduino headers needed; the game world is the one baked into include/world_data.hpp.

#include <cstdio>
#include <string>
#include "../include/poudre_trail_engine.hpp"

static int passed = 0;
static int failed = 0;

#define CHECK(cond) do { \
    if (cond) { ++passed; printf("  PASS: %s\n", #cond); } \
    else      { ++failed; printf("  FAIL: %s  (%s:%d)\n", #cond, __FILE__, __LINE__); } \
} while(0)

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// Resolve whatever is on screen with option 0 until the game is in TRAVEL (or ended).
static void clearQueue(GameEngine& g) {
    for (int guard = 0; guard < 40; ++guard) {
        GameMode m = g.snapshot().mode;
        if (m == GameMode::EVENT || m == GameMode::RESULT) g.chooseOption(0);
        else return;
    }
}

// Like clearQueue but stops (without resolving) when the named event is showing.
static bool runUntilHeadline(GameEngine& g, const std::string& headline) {
    for (int guard = 0; guard < 60; ++guard) {
        Snapshot s = g.snapshot();
        if (s.mode == GameMode::EVENT && s.headline == headline) return true;
        if (s.mode == GameMode::EVENT || s.mode == GameMode::RESULT) g.chooseOption(0);
        else if (s.mode == GameMode::TRAVEL) g.travel();
        else return false;
    }
    return false;
}

static void test_world_and_start() {
    printf("\n[1] embedded world loads; Janis event fires at start\n");
    GameEngine g;
    CHECK(g.route().size() == 6);
    CHECK(g.snapshot().mode == GameMode::TITLE);
    g.startGame();
    Snapshot s = g.snapshot();
    CHECK(s.mode == GameMode::EVENT);
    CHECK(s.locationId == "laporte");
    CHECK(s.headline == "Janis at the Store");
    CHECK(s.optionLabels.size() == 3);
    CHECK(s.dateLabel == "May 12, 1864");
}

static void test_result_flow() {
    printf("\n[2] choice -> RESULT with deltas -> Continue -> next queued event -> TRAVEL\n");
    GameEngine g;
    g.startGame();
    g.chooseOption(0);                       // buy flour
    Snapshot r = g.snapshot();
    CHECK(r.mode == GameMode::RESULT);
    CHECK(r.optionLabels.size() == 1);
    CHECK(contains(r.body, "Food +10"));
    CHECK(r.resources.food == 110);
    g.chooseOption(0);                       // continue
    Snapshot n = g.snapshot();
    CHECK(n.mode == GameMode::EVENT);        // ames_survey requires met_janis
    CHECK(n.headline == "A Map With Blanks");
    g.chooseOption(2);
    g.chooseOption(0);
    CHECK(g.snapshot().mode == GameMode::TRAVEL);
    CHECK(g.hasFlag("met_janis"));
    CHECK(g.hasFlag("map_silent"));
}

static void test_calendar_and_travel() {
    printf("\n[3] travel advances days and the calendar\n");
    GameEngine g;
    g.startGame();
    clearQueue(g);
    int d0 = g.snapshot().day;
    int f0 = g.snapshot().resources.food;
    g.travel();                              // Laporte -> Canyon Mouth, 6 miles = 2 days
    Snapshot s = g.snapshot();
    CHECK(s.locationId == "canyon_mouth");
    CHECK(s.day == d0 + 2);
    CHECK(s.dateLabel == "May 14, 1864");
    CHECK(s.resources.food <= f0 - 18);
    GameEngine late;
    late.loadSaveText("food=100\nday=21\nrouteIndex=0\n");
    CHECK(late.snapshot().dateLabel == "Jun 1, 1864");
}

static void test_timed_flood() {
    printf("\n[4] the flood arrives once the calendar reaches May 19\n");
    GameEngine g;
    g.seed(7);
    g.startGame();
    bool hit = runUntilHeadline(g, "High Water");
    CHECK(hit);
    CHECK(g.snapshot().day >= 8);
    g.chooseOption(1);                       // help pull people from the bank
    CHECK(g.hasFlag("helped_flood"));
    // the flood only ever fires once
    clearQueue(g);
    bool again = runUntilHeadline(g, "High Water");
    CHECK(!again);
}

static void test_save_load() {
    printf("\n[5] save/load round-trip keeps resources, reputation, flags, seen events\n");
    GameEngine a;
    a.seed(42);
    a.startGame();
    a.chooseOption(0); a.chooseOption(0);    // Janis + continue
    a.chooseOption(0); a.chooseOption(0);    // Ames + continue
    CHECK(a.snapshot().mode == GameMode::TRAVEL);
    std::string save = a.serializeSave();
    GameEngine b;
    CHECK(b.loadSaveText(save));
    CHECK(b.serializeSave() == save);
    Snapshot sa = a.snapshot(), sb = b.snapshot();
    CHECK(sb.mode == GameMode::TRAVEL);      // seen events are not replayed
    CHECK(sb.resources.repJanis == sa.resources.repJanis);
    CHECK(sb.resources.medicine == sa.resources.medicine);
    CHECK(b.hasFlag("map_helped_ames"));
    CHECK(!b.loadSaveText(""));
}

static void test_camp_and_trade() {
    printf("\n[6] camp (rest / hunt) and trading posts\n");
    GameEngine g;
    g.seed(3);
    g.startGame();
    clearQueue(g);
    Snapshot s0 = g.snapshot();
    g.openCamp();
    CHECK(g.snapshot().mode == GameMode::CAMP);
    CHECK(g.snapshot().optionLabels.size() == 4);
    g.chooseOption(0);                       // rest
    Snapshot s1 = g.snapshot();
    CHECK(s1.mode == GameMode::RESULT);
    CHECK(s1.day == s0.day + 1);
    CHECK(s1.resources.food == s0.resources.food - 9);
    g.chooseOption(0);
    CHECK(g.snapshot().mode == GameMode::TRAVEL);
    g.openCamp();
    int ammo = g.snapshot().resources.ammunition;
    g.chooseOption(1);                       // hunt
    CHECK(g.snapshot().resources.ammunition == ammo - 3);
    g.chooseOption(0);
    // trading
    g.openTrade();
    CHECK(g.snapshot().mode == GameMode::TRADE);
    int food = g.snapshot().resources.food, goods = g.snapshot().resources.tradeGoods;
    g.chooseOption(0);
    CHECK(g.snapshot().resources.food == food + 20);
    CHECK(g.snapshot().resources.tradeGoods == goods - 1);
    CHECK(g.snapshot().mode == GameMode::TRADE);   // stays so several trades can be made
    g.chooseOption(99);                      // leave
    CHECK(g.snapshot().mode == GameMode::TRAVEL);
    // refusing a trade you cannot afford
    GameEngine broke;
    broke.loadSaveText("food=50\ntradeGoods=0\nrouteIndex=0\nday=1\nseen=janis_store,ames_survey\n");
    clearQueue(broke);
    broke.openTrade();
    broke.chooseOption(0);
    CHECK(broke.snapshot().resources.food == 50);
}

static void test_game_over_guard() {
    printf("\n[7] starvation ends the run; travelTo is a no-op afterwards\n");
    GameEngine g;
    g.loadSaveText("food=4\nammunition=20\nmedicine=2\nhealth=85\nmorale=70\nhorseCondition=100\n"
                   "tradeGoods=6\nrouteIndex=0\nday=1\nseen=janis_store,ames_survey\n");
    clearQueue(g);
    g.travel();                              // 2 days of food = 10 > 4
    Snapshot s = g.snapshot();
    CHECK(s.mode == GameMode::GAME_OVER);
    CHECK(contains(s.body, "food"));
    g.travelTo("fort_collins");
    CHECK(g.snapshot().mode == GameMode::GAME_OVER);
    CHECK(g.snapshot().locationId == "laporte");
}

static void test_full_run_victory() {
    printf("\n[8] a full run reaches Fort Collins with an epilogue and history note\n");
    GameEngine g;
    g.seed(11);
    g.startGame();
    for (int guard = 0; guard < 120 && g.snapshot().mode != GameMode::VICTORY
                                     && g.snapshot().mode != GameMode::GAME_OVER; ++guard) {
        Snapshot s = g.snapshot();
        if (s.mode == GameMode::EVENT || s.mode == GameMode::RESULT) g.chooseOption(0);
        else if (s.mode == GameMode::TRAVEL) {
            if (s.resources.food < 40) { g.openCamp(); g.chooseOption(1); g.chooseOption(0); }
            else g.travel();
        } else if (s.mode == GameMode::CAMP) g.closeCamp();
    }
    Snapshot v = g.snapshot();
    CHECK(v.mode == GameMode::VICTORY);
    CHECK(v.locationId == "fort_collins");
    CHECK(contains(v.body, "History:"));
    CHECK(v.headline == "A Map With Names");
    g.travelTo("laporte");
    CHECK(g.snapshot().mode == GameMode::VICTORY);
}

static void test_contradiction() {
    printf("\n[9] serving both Mason and Friday sets moral_contradiction\n");
    GameEngine g;
    g.loadSaveText("food=100\nrepFriday=6\nrepMason=0\nrouteIndex=4\nday=5\n");
    Snapshot s = g.snapshot();
    CHECK(s.mode == GameMode::EVENT);
    CHECK(s.headline == "Mason Makes His Choice");
    g.chooseOption(0);                       // trade with him anyway: +5 Mason
    CHECK(g.hasFlag("moral_contradiction"));
    g.chooseOption(0);
    CHECK(g.snapshot().mode == GameMode::EVENT);     // witness paper (traded_with_mason)
}

static void test_finale_gating() {
    printf("\n[10] exactly one Fort Collins finale fires\n");
    GameEngine g;
    g.loadSaveText("food=100\nflags=traded_with_mason,signed_claim\nseen=high_water\nrouteIndex=5\nday=12\n");
    Snapshot s = g.snapshot();
    CHECK(s.mode == GameMode::EVENT);
    CHECK(s.headline == "Mason's Line");
    g.chooseOption(0);                       // let it stand
    g.chooseOption(0);                       // continue
    Snapshot e = g.snapshot();
    CHECK(e.mode == GameMode::VICTORY);
    CHECK(e.headline == "Mason's Line");
    CHECK(g.hasFlag("finale_done"));
}

static void test_determinism() {
    printf("\n[11] same seed and choices give the same run\n");
    auto play = [](uint32_t seed) {
        GameEngine g; g.seed(seed); g.startGame();
        for (int i = 0; i < 40; ++i) {
            Snapshot s = g.snapshot();
            if (s.mode == GameMode::EVENT || s.mode == GameMode::RESULT) g.chooseOption(0);
            else if (s.mode == GameMode::TRAVEL) g.travel();
            else break;
        }
        return g.serializeSave();
    };
    CHECK(play(5) == play(5));
}

int main() {
    test_world_and_start();
    test_result_flow();
    test_calendar_and_travel();
    test_timed_flood();
    test_save_load();
    test_camp_and_trade();
    test_game_over_guard();
    test_full_run_victory();
    test_contradiction();
    test_finale_gating();
    test_determinism();
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
