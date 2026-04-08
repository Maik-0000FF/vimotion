#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/instance.h>
#include <testfrontend_public.h>

using namespace fcitx;

static std::vector<Key> forwarded;
static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    do { std::cerr << "  " << (name) << "... "; } while(0)

#define EXPECT_FORWARDED(expected_sym) \
    do { \
        if (forwarded.empty()) { \
            std::cerr << "FAIL (no key forwarded)\n"; testsFailed++; \
        } else if (forwarded.back().sym() != (expected_sym)) { \
            std::cerr << "FAIL (expected " << #expected_sym \
                      << ", got sym=" << forwarded.back().sym() << ")\n"; \
            testsFailed++; \
        } else { \
            std::cerr << "OK\n"; testsPassed++; \
        } \
    } while(0)

#define EXPECT_FORWARDED_COUNT(n) \
    do { \
        if (forwarded.size() != static_cast<size_t>(n)) { \
            std::cerr << "FAIL (expected " << (n) << " keys, got " \
                      << forwarded.size() << ")\n"; testsFailed++; \
        } else { \
            std::cerr << "OK\n"; testsPassed++; \
        } \
    } while(0)

#define EXPECT_NO_FORWARD() \
    do { \
        if (!forwarded.empty()) { \
            std::cerr << "FAIL (expected no forward, got " \
                      << forwarded.size() << " keys)\n"; testsFailed++; \
        } else { \
            std::cerr << "OK\n"; testsPassed++; \
        } \
    } while(0)

#define EXPECT_ACCEPTED(ev) \
    do { \
        if ((ev)) { \
            std::cerr << "OK\n"; testsPassed++; \
        } else { \
            std::cerr << "FAIL (event not accepted)\n"; testsFailed++; \
        } \
    } while(0)

#define EXPECT_NOT_ACCEPTED(ev) \
    do { \
        if (!(ev)) { \
            std::cerr << "OK\n"; testsPassed++; \
        } else { \
            std::cerr << "FAIL (event was accepted, should pass through)\n"; \
            testsFailed++; \
        } \
    } while(0)

static void sendKey(AddonInstance *frontend, const ICUUID &uuid,
                    KeySym sym, KeyStates states = KeyState::NoState) {
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(sym, states), false);
}

static bool sendKeyAccepted(AddonInstance *frontend, const ICUUID &uuid,
                            KeySym sym, KeyStates states = KeyState::NoState) {
    forwarded.clear();
    return frontend->call<ITestFrontend::sendKeyEvent>(
        uuid, Key(sym, states), false);
}

static void runTests(Instance *instance) {
    auto *frontend = instance->addonManager().addon("testfrontend");
    if (!frontend) {
        std::cerr << "FATAL: testfrontend nicht geladen\n";
        instance->exit();
        return;
    }

    // Module laden
    auto *vimod = instance->addonManager().addon("vimotion-module", true);
    if (!vimod) {
        std::cerr << "FATAL: vimotion-module nicht geladen\n";
        instance->exit();
        return;
    }

    // InputContext erstellen und fokussieren
    auto uuid = frontend->call<ITestFrontend::createInputContext>("");
    auto *ic = instance->inputContextManager().findByUUID(uuid);
    if (ic) {
        ic->focusIn();
    }

    // ForwardKey-Events abfangen
    auto handler = instance->watchEvent(
        EventType::InputContextForwardKey,
        EventWatcherPhase::Default,
        [](Event &event) {
            auto &fke = static_cast<ForwardKeyEvent &>(event);
            if (!fke.isRelease()) {
                forwarded.push_back(fke.rawKey());
            }
        });

    // ====== Module ist initial deaktiviert ======
    std::cerr << "\n=== Module: Initial State ===\n";

    TEST("Module initial deaktiviert: Tasten gehen durch");
    sendKey(frontend, uuid, FcitxKey_h);
    EXPECT_NO_FORWARD();

    TEST("Buchstabe 'j' geht durch (nicht konsumiert)");
    sendKey(frontend, uuid, FcitxKey_j);
    EXPECT_NO_FORWARD();

    // ====== Toggle mit Ctrl+Escape ======
    std::cerr << "\n=== Toggle ===\n";

    TEST("Ctrl+Escape aktiviert Module");
    sendKey(frontend, uuid, FcitxKey_Escape, KeyState::Ctrl);
    EXPECT_NO_FORWARD(); // Toggle selbst forwarded nichts

    TEST("Nach Toggle: h -> Left (Normal Mode aktiv)");
    sendKey(frontend, uuid, FcitxKey_h);
    EXPECT_FORWARDED(FcitxKey_Left);

    TEST("Nach Toggle: j -> Down");
    sendKey(frontend, uuid, FcitxKey_j);
    EXPECT_FORWARDED(FcitxKey_Down);

    TEST("Ctrl+Escape deaktiviert Module");
    sendKey(frontend, uuid, FcitxKey_Escape, KeyState::Ctrl);
    EXPECT_NO_FORWARD();

    TEST("Nach Deaktivierung: h geht durch (nicht konsumiert)");
    sendKey(frontend, uuid, FcitxKey_h);
    EXPECT_NO_FORWARD();

    // Wieder aktivieren fuer weitere Tests
    sendKey(frontend, uuid, FcitxKey_Escape, KeyState::Ctrl);

    // ====== Normal Mode Motions ======
    std::cerr << "\n=== Module: Normal Mode Motions ===\n";

    TEST("h -> Left");
    sendKey(frontend, uuid, FcitxKey_h);
    EXPECT_FORWARDED(FcitxKey_Left);

    TEST("l -> Right");
    sendKey(frontend, uuid, FcitxKey_l);
    EXPECT_FORWARDED(FcitxKey_Right);

    TEST("k -> Up");
    sendKey(frontend, uuid, FcitxKey_k);
    EXPECT_FORWARDED(FcitxKey_Up);

    TEST("w -> Ctrl+Right");
    sendKey(frontend, uuid, FcitxKey_w);
    EXPECT_FORWARDED(FcitxKey_Right);

    TEST("b -> Ctrl+Left");
    sendKey(frontend, uuid, FcitxKey_b);
    EXPECT_FORWARDED(FcitxKey_Left);

    TEST("0 -> Home");
    sendKey(frontend, uuid, FcitxKey_0);
    EXPECT_FORWARDED(FcitxKey_Home);

    TEST("$ -> End");
    sendKey(frontend, uuid, FcitxKey_dollar);
    EXPECT_FORWARDED(FcitxKey_End);

    TEST("G -> Ctrl+End");
    sendKey(frontend, uuid, FcitxKey_G);
    EXPECT_FORWARDED(FcitxKey_End);

    TEST("e -> Ctrl+Right, Left (2 keys)");
    sendKey(frontend, uuid, FcitxKey_e);
    EXPECT_FORWARDED_COUNT(2);

    TEST("gg -> Ctrl+Home");
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_g), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_g), false);
    EXPECT_FORWARDED(FcitxKey_Home);

    // ====== Count-Prefix ======
    std::cerr << "\n=== Module: Count-Prefix ===\n";

    TEST("3j -> 3x Down");
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_3), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_j), false);
    EXPECT_FORWARDED_COUNT(3);

    TEST("10j -> 10x Down");
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_1), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_0), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_j), false);
    EXPECT_FORWARDED_COUNT(10);

    // ====== Insert Mode ======
    std::cerr << "\n=== Module: Insert Mode ===\n";

    TEST("i -> Insert Mode (kein Forward)");
    sendKey(frontend, uuid, FcitxKey_i);
    EXPECT_NO_FORWARD();

    TEST("Insert Mode: Taste geht durch (an IM weiter)");
    sendKey(frontend, uuid, FcitxKey_x);
    EXPECT_NO_FORWARD();

    TEST("Insert Mode: Escape -> zurueck zu Normal");
    sendKey(frontend, uuid, FcitxKey_Escape);
    EXPECT_NO_FORWARD();

    TEST("Nach Escape: h -> Left (Normal Mode)");
    sendKey(frontend, uuid, FcitxKey_h);
    EXPECT_FORWARDED(FcitxKey_Left);

    // ====== Einfache Befehle ======
    std::cerr << "\n=== Module: Einfache Befehle ===\n";

    TEST("x -> Delete");
    sendKey(frontend, uuid, FcitxKey_x);
    EXPECT_FORWARDED(FcitxKey_Delete);

    TEST("X -> BackSpace");
    sendKey(frontend, uuid, FcitxKey_X);
    EXPECT_FORWARDED(FcitxKey_BackSpace);

    TEST("u -> Ctrl+Z");
    sendKey(frontend, uuid, FcitxKey_u);
    EXPECT_FORWARDED(FcitxKey_z);

    TEST("Ctrl+R -> Ctrl+Y (Redo)");
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(
        uuid, Key(FcitxKey_r, KeyState::Ctrl), false);
    EXPECT_FORWARDED(FcitxKey_y);

    // ====== Operatoren ======
    std::cerr << "\n=== Module: Operatoren ===\n";

    TEST("dw -> Shift+Ctrl+Right, Delete");
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_d), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_w), false);
    EXPECT_FORWARDED_COUNT(2);

    TEST("dd -> Home, Shift+Down, Delete");
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_d), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_d), false);
    EXPECT_FORWARDED_COUNT(3);

    TEST("yy -> Home, Shift+Down, Ctrl+C, Left");
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_y), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_y), false);
    EXPECT_FORWARDED_COUNT(4);

    TEST("cw -> Shift+Ctrl+Right, Delete (+ Insert Mode)");
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_c), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_w), false);
    EXPECT_FORWARDED_COUNT(2);
    sendKey(frontend, uuid, FcitxKey_Escape);

    TEST("3dd -> Home, 3x Shift+Down, Delete");
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_3), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_d), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_d), false);
    EXPECT_FORWARDED_COUNT(5);

    TEST("d + Escape -> abbrechen");
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_d), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Escape), false);
    EXPECT_NO_FORWARD();

    // ====== Insert-Einstiege ======
    std::cerr << "\n=== Module: Insert-Einstiege ===\n";

    TEST("a -> Right + Insert");
    sendKey(frontend, uuid, FcitxKey_a);
    EXPECT_FORWARDED(FcitxKey_Right);
    sendKey(frontend, uuid, FcitxKey_Escape);

    TEST("A -> End + Insert");
    sendKey(frontend, uuid, FcitxKey_A);
    EXPECT_FORWARDED(FcitxKey_End);
    sendKey(frontend, uuid, FcitxKey_Escape);

    TEST("I -> Home + Insert");
    sendKey(frontend, uuid, FcitxKey_I);
    EXPECT_FORWARDED(FcitxKey_Home);
    sendKey(frontend, uuid, FcitxKey_Escape);

    TEST("o -> End + Return + Insert");
    sendKey(frontend, uuid, FcitxKey_o);
    EXPECT_FORWARDED_COUNT(2);
    sendKey(frontend, uuid, FcitxKey_Escape);

    TEST("O -> Home + Return + Up + Insert");
    sendKey(frontend, uuid, FcitxKey_O);
    EXPECT_FORWARDED_COUNT(3);
    sendKey(frontend, uuid, FcitxKey_Escape);

    // ====== Paste ======
    std::cerr << "\n=== Module: Paste ===\n";

    TEST("p -> End, Return, Ctrl+V");
    sendKey(frontend, uuid, FcitxKey_p);
    EXPECT_FORWARDED_COUNT(3);

    TEST("P -> Home, Return, Up, Ctrl+V");
    sendKey(frontend, uuid, FcitxKey_P);
    EXPECT_FORWARDED_COUNT(4);

    // ====== Pass-through ======
    std::cerr << "\n=== Module: Pass-through ===\n";

    TEST("Ctrl+S durchgelassen");
    sendKey(frontend, uuid, FcitxKey_s, KeyState::Ctrl);
    EXPECT_NO_FORWARD();

    TEST("Pfeiltaste Left durchgelassen");
    sendKey(frontend, uuid, FcitxKey_Left);
    EXPECT_NO_FORWARD();

    TEST("F1 durchgelassen");
    sendKey(frontend, uuid, FcitxKey_F1);
    EXPECT_NO_FORWARD();

    TEST("Return durchgelassen");
    sendKey(frontend, uuid, FcitxKey_Return);
    EXPECT_NO_FORWARD();

    TEST("BackSpace durchgelassen");
    sendKey(frontend, uuid, FcitxKey_BackSpace);
    EXPECT_NO_FORWARD();

    // ====== Per-IC State: zweiter InputContext ======
    std::cerr << "\n=== Module: Per-IC State ===\n";

    auto uuid2 = frontend->call<ITestFrontend::createInputContext>("");
    auto *ic2 = instance->inputContextManager().findByUUID(uuid2);
    if (ic2) {
        ic2->focusIn();
    }

    TEST("Zweiter IC: Module nicht aktiv (eigener State)");
    sendKey(frontend, uuid2, FcitxKey_h);
    EXPECT_NO_FORWARD();

    TEST("Zweiter IC: Toggle aktiviert nur diesen IC");
    sendKey(frontend, uuid2, FcitxKey_Escape, KeyState::Ctrl);
    sendKey(frontend, uuid2, FcitxKey_h);
    EXPECT_FORWARDED(FcitxKey_Left);

    TEST("Erster IC: noch im Normal Mode (unabhaengig)");
    sendKey(frontend, uuid, FcitxKey_j);
    EXPECT_FORWARDED(FcitxKey_Down);

    frontend->call<ITestFrontend::destroyInputContext>(uuid2);

    // ====== Blacklist: neuer IC mit vim-Programm ======
    std::cerr << "\n=== Module: Blacklist ===\n";

    auto uuidVim = frontend->call<ITestFrontend::createInputContext>("nvim");
    auto *icVim = instance->inputContextManager().findByUUID(uuidVim);
    if (icVim) {
        icVim->focusIn();
    }

    TEST("Blacklisted App (nvim): Ctrl+Escape wird konsumiert aber nicht aktiviert");
    sendKey(frontend, uuidVim, FcitxKey_Escape, KeyState::Ctrl);
    EXPECT_NO_FORWARD();

    TEST("Blacklisted App: h geht durch (kein vimotion)");
    sendKey(frontend, uuidVim, FcitxKey_h);
    EXPECT_NO_FORWARD();

    frontend->call<ITestFrontend::destroyInputContext>(uuidVim);

    // ====== Zusammenfassung ======
    std::cerr << "\n=============================\n";
    std::cerr << "Passed: " << testsPassed << "\n";
    std::cerr << "Failed: " << testsFailed << "\n";
    std::cerr << "=============================\n";

    frontend->call<ITestFrontend::destroyInputContext>(uuid);

    if (testsFailed > 0) {
        instance->exit();
        exit(1);
    }
    instance->exit();
}

int main() {
    setupTestingEnvironment(
        TESTING_BINARY_DIR,
        {"addon", "module"},
        {"tests"});

    char arg0[] = "test_vimotion_module";
    char *argv[] = {arg0};
    Instance instance(1, argv);
    instance.addonManager().registerDefaultLoader(nullptr);

    EventDispatcher dispatcher;
    dispatcher.attach(&instance.eventLoop());
    dispatcher.schedule([&instance]() {
        runTests(&instance);
    });

    try {
        return instance.exec();
    } catch (const InstanceQuietQuit &) {
        return 0;
    }
}
