// Host-side test harness for poudre_trail_engine.
// Build and run: cd test && make && ./test_engine
//
// Tests (all compile and run without Arduino headers):
//   1. travelTo VICTORY/GAME_OVER is a no-op
//   2. starvation → GAME_OVER not overwritten by subsequent travel
//   3. save/load round-trip preserves ammo, medicine, three rep values
//   4. moral_contradiction route sets flag; victory footer contains "contradiction"
//   5. startGame queues Janis event at Laporte (mode=EVENT, headline matches)

#include <cassert>
#include <cstdio>
#include "../include/poudre_trail_engine.hpp"

static int passed = 0;
static int failed = 0;

#define CHECK(cond) do { \
    if (cond) { ++passed; printf("  PASS: %s\n", #cond); } \
    else      { ++failed; printf("  FAIL: %s  (%s:%d)\n", #cond, __FILE__, __LINE__); } \
} while(0)

// ─── Test 1: travelTo is a no-op in VICTORY/GAME_OVER ────────────────────────
static void test_travel_guard() {
    printf("\n[1] travelTo no-op in VICTORY/GAME_OVER\n");
    GameEngine eng;

    // Reach VICTORY by travelling through all stops
    eng.startGame();
    eng.chooseOption(0); // resolve Janis event
    eng.travelTo("council_tree");
    eng.chooseOption(0); // resolve Chief Friday event
    eng.travelTo("virginia_dale");
    eng.chooseOption(0); // resolve Virginia Dale event
    eng.travelTo("mason_claim");
    eng.chooseOption(0); // resolve Mason event
    eng.travelTo("fort_collins"); // → VICTORY

    Snapshot before = eng.snapshot();
    CHECK(before.mode == GameMode::VICTORY);
    eng.travelTo("laporte"); // should be no-op
    Snapshot after = eng.snapshot();
    CHECK(after.mode == GameMode::VICTORY);
    CHECK(after.locationId == before.locationId);

    // GAME_OVER guard
    GameEngine eng2;
    eng2.startGame();
    eng2.chooseOption(0);
    // Force food to 0 via serialization trick: manipulate through save/load
    std::string sv = eng2.serializeSave();
    sv += "food=0\n"; // override (parseKV last-wins for duplicate keys)
    // Actually parseKV won't deduplicate; set food via re-serialize
    // Instead, just save a custom save string
    std::string customSave = "food=0\nammunition=30\nmedicine=3\nhealth=85\nmorale=70\nhorseCondition=100\ntradeGoods=6\nrepJanis=0\nrepFriday=0\nrepMason=0\nrouteIndex=1\nday=4\nflag_moral_contradiction=0\n";
    eng2.loadSaveText(customSave);
    // clampResources should have set GAME_OVER on load
    Snapshot go = eng2.snapshot();
    CHECK(go.mode == GameMode::GAME_OVER);
    eng2.travelTo("virginia_dale");
    CHECK(eng2.snapshot().mode == GameMode::GAME_OVER);
}

// ─── Test 2: starvation GAME_OVER not overwritten ────────────────────────────
static void test_starvation_not_overwritten() {
    printf("\n[2] starvation GAME_OVER not overwritten by travelTo\n");
    GameEngine eng;
    eng.startGame();
    eng.chooseOption(0); // leave Janis event

    // Load save with food=0 to trigger starvation on next travel
    std::string sv = "food=1\nammunition=30\nmedicine=3\nhealth=85\nmorale=70\nhorseCondition=100\ntradeGoods=6\nrepJanis=0\nrepFriday=0\nrepMason=0\nrouteIndex=0\nday=1\nflag_moral_contradiction=0\n";
    eng.loadSaveText(sv);

    // Now travelTo council_tree: food -= 12 → food goes to 0 → GAME_OVER
    eng.travelTo("council_tree");
    Snapshot s = eng.snapshot();
    CHECK(s.mode == GameMode::GAME_OVER);

    // Calling travelTo again must not change the mode
    eng.travelTo("virginia_dale");
    CHECK(eng.snapshot().mode == GameMode::GAME_OVER);
    CHECK(eng.snapshot().locationId != "virginia_dale" || eng.snapshot().mode == GameMode::GAME_OVER);
}

// ─── Test 3: save/load round-trip ────────────────────────────────────────────
static void test_save_load_roundtrip() {
    printf("\n[3] save/load round-trip preserves ammo, medicine, three reps\n");
    GameEngine eng;
    eng.startGame();
    // Apply Janis choice 1 (gives food+10, trade-1, repJanis+4, sets met_janis)
    eng.chooseOption(0);
    // Travel to council_tree and take choice 0 (repFriday+5)
    eng.travelTo("council_tree");
    eng.chooseOption(0);
    // Travel to virginia_dale and take choice 0
    eng.travelTo("virginia_dale");
    eng.chooseOption(0);

    std::string sv = eng.serializeSave();

    GameEngine eng2;
    std::string err;
    bool ok = eng2.loadSaveText(sv, &err);
    CHECK(ok);
    CHECK(err.empty());

    Snapshot orig = eng.snapshot();
    Snapshot rest = eng2.snapshot();

    CHECK(rest.resources.ammunition    == orig.resources.ammunition);
    CHECK(rest.resources.medicine      == orig.resources.medicine);
    CHECK(rest.resources.repJanis      == orig.resources.repJanis);
    CHECK(rest.resources.repFriday     == orig.resources.repFriday);
    CHECK(rest.resources.repMason      == orig.resources.repMason);
    CHECK(rest.resources.food          == orig.resources.food);
}

// ─── Test 4: contradiction route sets flag; victory footer ───────────────────
static void test_contradiction_route() {
    printf("\n[4] contradiction route sets moral_contradiction flag\n");
    // Need repMason >= 5 AND repFriday >= 5
    // mason_reveal choice 0 gives repMason+5, repFriday-1
    // chief_friday choice 0 gives repFriday+5
    // So: play chief_friday choice 0 first (+5 Friday), then mason choice 0 (+5 Mason, -1 Friday → Friday=4)
    // That gives Friday=4, Mason=5 — no flag.
    // Need another source of Friday rep. chief_friday choice 1 gives repFriday+3.
    // Let's do: janis choice 1 (+repJanis+2, +repFriday+1), then chief_friday choice 0 (+repFriday+5=6), mason choice 0 (+repMason+5, repFriday-1=5) → both >= 5
    GameEngine eng;
    eng.startGame();
    eng.chooseOption(1); // Janis choice 1: repJanis+2, repFriday+1
    eng.travelTo("council_tree");
    eng.chooseOption(0); // Chief Friday choice 0: repFriday+5 → Friday=6
    eng.travelTo("virginia_dale");
    eng.chooseOption(0); // Virginia choice 0: repFriday+1 → Friday=7
    eng.travelTo("mason_claim");
    eng.chooseOption(0); // Mason choice 0: repMason+5, repFriday-1 → Mason=5, Friday=6
    // evaluateReputation: Mason>=5 AND Friday>=5 → moral_contradiction=true

    // Now serialize to check flag
    std::string sv = eng.serializeSave();
    CHECK(sv.find("flag_moral_contradiction=1") != std::string::npos);

    eng.travelTo("fort_collins");
    Snapshot s = eng.snapshot();
    CHECK(s.mode == GameMode::VICTORY);
    CHECK(s.footer.find("contradiction") != std::string::npos);
}

// ─── Test 5: Janis event at start ────────────────────────────────────────────
static void test_janis_at_start() {
    printf("\n[5] startGame queues Janis event at Laporte\n");
    GameEngine eng;
    eng.startGame();
    Snapshot s = eng.snapshot();
    CHECK(s.mode == GameMode::EVENT);
    CHECK(s.locationId == "laporte");
    CHECK(!s.headline.empty());
    // headline should contain "Janis" (from fallback world)
    CHECK(s.headline.find("Janis") != std::string::npos);
    CHECK(!s.optionLabels.empty());
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    printf("=== Poudre Trail Engine Tests ===\n");
    test_travel_guard();
    test_starvation_not_overwritten();
    test_save_load_roundtrip();
    test_contradiction_route();
    test_janis_at_start();
    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
