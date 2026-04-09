// Test suite for the vimotion module addon.
//
// Coverage:
//   1- Basis: Toggle, Normal-Mode-Motions, Count-Prefix
//   2- Insert/OperatorPending, Operatoren, Paste, Pass-through
//   3- Per-IC State, Default Blacklist
//   4- Config-Roundtrip: custom Toggle Key, EnabledByDefault, Whitelist
//   5- Insert-Mode Mappings (jk -> Escape) inkl. Flush-Verhalten

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <fcitx-config/rawconfig.h>
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

#define EXPECT_FORWARDED_FRONT(expected_sym) \
    do { \
        if (forwarded.empty()) { \
            std::cerr << "FAIL (no key forwarded)\n"; testsFailed++; \
        } else if (forwarded.front().sym() != (expected_sym)) { \
            std::cerr << "FAIL (expected " << #expected_sym \
                      << ", got sym=" << forwarded.front().sym() << ")\n"; \
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

#define EXPECT_TRUE(cond) \
    do { \
        if (cond) { \
            std::cerr << "OK\n"; testsPassed++; \
        } else { \
            std::cerr << "FAIL (" #cond " was false)\n"; testsFailed++; \
        } \
    } while(0)

#define EXPECT_FALSE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "OK\n"; testsPassed++; \
        } else { \
            std::cerr << "FAIL (" #cond " was true)\n"; testsFailed++; \
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

static void resetConfigDefault(AddonInstance *vimod) {
    RawConfig cfg;
    cfg.setValueByPath("General/EnabledByDefault", "False");
    cfg.setValueByPath("General/ToggleKey/0", "Control+Escape");
    cfg.setValueByPath("AppFilter/Mode", "Blacklist");
    cfg.setValueByPath("AppFilter/Blacklist/0", "nvim");
    cfg.setValueByPath("AppFilter/Blacklist/1", "vim");
    cfg.setValueByPath("AppFilter/Blacklist/2", "neovim");
    cfg.setValueByPath("Mappings/TimeoutMs", "200");
    cfg.setValueByPath("Mappings/InsertMap/0", "jk=Escape");
    vimod->setConfig(cfg);
}

static void runTests(Instance *instance) {
    auto *frontend = instance->addonManager().addon("testfrontend");
    if (!frontend) {
        std::cerr << "FATAL: testfrontend nicht geladen\n";
        instance->exit();
        return;
    }

    auto *vimod = instance->addonManager().addon("vimotion-module", true);
    if (!vimod) {
        std::cerr << "FATAL: vimotion-module nicht geladen\n";
        testsFailed++;
        instance->exit();
        return;
    }

    // Defaults setzen (Config-Roundtrip ist gleichzeitig erste Validierung)
    resetConfigDefault(vimod);

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

    // ====== Initial deaktiviert ======
    std::cerr << "\n=== Initial State ===\n";

    TEST("Module initial deaktiviert: h geht durch");
    sendKey(frontend, uuid, FcitxKey_h);
    EXPECT_NO_FORWARD();

    TEST("Buchstabe 'j' geht durch (nicht konsumiert)");
    sendKey(frontend, uuid, FcitxKey_j);
    EXPECT_NO_FORWARD();

    // ====== Toggle mit Ctrl+Escape (Default) ======
    std::cerr << "\n=== Toggle (Default Hotkey) ===\n";

    TEST("Ctrl+Escape aktiviert Module");
    sendKey(frontend, uuid, FcitxKey_Escape, KeyState::Ctrl);
    EXPECT_NO_FORWARD();

    TEST("Nach Toggle: h -> Left (Normal Mode aktiv)");
    sendKey(frontend, uuid, FcitxKey_h);
    EXPECT_FORWARDED(FcitxKey_Left);

    TEST("Nach Toggle: j -> Down");
    sendKey(frontend, uuid, FcitxKey_j);
    EXPECT_FORWARDED(FcitxKey_Down);

    TEST("Ctrl+Escape deaktiviert Module");
    sendKey(frontend, uuid, FcitxKey_Escape, KeyState::Ctrl);
    EXPECT_NO_FORWARD();

    TEST("Nach Deaktivierung: h geht durch");
    sendKey(frontend, uuid, FcitxKey_h);
    EXPECT_NO_FORWARD();

    // Wieder aktivieren fuer weitere Tests
    sendKey(frontend, uuid, FcitxKey_Escape, KeyState::Ctrl);

    // ====== Normal Mode Motions ======
    std::cerr << "\n=== Normal Mode Motions ===\n";

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
    std::cerr << "\n=== Count-Prefix ===\n";

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

    // ====== Insert Mode (ohne Mappings) ======
    std::cerr << "\n=== Insert Mode (ohne Sequenzmatching) ===\n";

    TEST("i -> Insert Mode (kein Forward)");
    sendKey(frontend, uuid, FcitxKey_i);
    EXPECT_NO_FORWARD();

    TEST("Insert Mode: 'x' geht durch (kein Match)");
    EXPECT_FALSE(sendKeyAccepted(frontend, uuid, FcitxKey_x));

    TEST("Insert Mode: Escape -> zurueck zu Normal");
    sendKey(frontend, uuid, FcitxKey_Escape);
    EXPECT_NO_FORWARD();

    TEST("Nach Escape: h -> Left (Normal Mode)");
    sendKey(frontend, uuid, FcitxKey_h);
    EXPECT_FORWARDED(FcitxKey_Left);

    // ====== Einfache Befehle ======
    std::cerr << "\n=== Einfache Befehle ===\n";

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
    std::cerr << "\n=== Operatoren ===\n";

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
    std::cerr << "\n=== Insert-Einstiege ===\n";

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
    std::cerr << "\n=== Paste ===\n";

    TEST("p -> End, Return, Ctrl+V");
    sendKey(frontend, uuid, FcitxKey_p);
    EXPECT_FORWARDED_COUNT(3);

    TEST("P -> Home, Return, Up, Ctrl+V");
    sendKey(frontend, uuid, FcitxKey_P);
    EXPECT_FORWARDED_COUNT(4);

    // ====== Pass-through ======
    std::cerr << "\n=== Pass-through ===\n";

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

    // ====== Per-IC State ======
    std::cerr << "\n=== Per-IC State ===\n";

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

    // ====== Default Blacklist ======
    std::cerr << "\n=== Default Blacklist (nvim) ===\n";

    auto uuidVim = frontend->call<ITestFrontend::createInputContext>("nvim");
    auto *icVim = instance->inputContextManager().findByUUID(uuidVim);
    if (icVim) {
        icVim->focusIn();
    }

    TEST("nvim (blacklist): Ctrl+Escape geht komplett durch");
    EXPECT_FALSE(sendKeyAccepted(frontend, uuidVim,
                                 FcitxKey_Escape, KeyState::Ctrl));

    TEST("nvim (blacklist): h geht durch");
    EXPECT_FALSE(sendKeyAccepted(frontend, uuidVim, FcitxKey_h));

    frontend->call<ITestFrontend::destroyInputContext>(uuidVim);

    // ====== Config: Custom Toggle Key ======
    std::cerr << "\n=== Config: Custom Toggle Key (F12) ===\n";

    {
        // Aktuellen IC deaktivieren
        sendKey(frontend, uuid, FcitxKey_Escape, KeyState::Ctrl);

        RawConfig cfg;
        cfg.setValueByPath("General/EnabledByDefault", "False");
        cfg.setValueByPath("General/ToggleKey/0", "F12");
        cfg.setValueByPath("AppFilter/Mode", "None");
        cfg.setValueByPath("Mappings/TimeoutMs", "200");
        cfg.setValueByPath("Mappings/InsertMap/0", "jk=Escape");
        vimod->setConfig(cfg);

        TEST("Alter Hotkey Ctrl+Escape ist nicht mehr aktiv");
        EXPECT_FALSE(sendKeyAccepted(frontend, uuid,
                                     FcitxKey_Escape, KeyState::Ctrl));

        TEST("F12 aktiviert Module");
        sendKey(frontend, uuid, FcitxKey_F12);
        EXPECT_NO_FORWARD();

        TEST("Nach F12-Toggle: h -> Left");
        sendKey(frontend, uuid, FcitxKey_h);
        EXPECT_FORWARDED(FcitxKey_Left);

        TEST("F12 deaktiviert Module wieder");
        sendKey(frontend, uuid, FcitxKey_F12);
        sendKey(frontend, uuid, FcitxKey_h);
        EXPECT_NO_FORWARD();
    }

    // ====== Config: EnabledByDefault ======
    std::cerr << "\n=== Config: EnabledByDefault (Auto-Start) ===\n";

    {
        RawConfig cfg;
        cfg.setValueByPath("General/EnabledByDefault", "True");
        cfg.setValueByPath("General/ToggleKey/0", "Control+Escape");
        cfg.setValueByPath("AppFilter/Mode", "None");
        cfg.setValueByPath("Mappings/TimeoutMs", "200");
        cfg.setValueByPath("Mappings/InsertMap/0", "jk=Escape");
        vimod->setConfig(cfg);

        auto uuidAuto = frontend->call<ITestFrontend::createInputContext>("");
        auto *icAuto = instance->inputContextManager().findByUUID(uuidAuto);
        if (icAuto) {
            icAuto->focusIn();
        }

        TEST("Neuer IC mit EnabledByDefault=True: h -> Left");
        sendKey(frontend, uuidAuto, FcitxKey_h);
        EXPECT_FORWARDED(FcitxKey_Left);

        TEST("Neuer IC: bereits in Normal Mode (i wechselt zu Insert)");
        sendKey(frontend, uuidAuto, FcitxKey_i);
        EXPECT_NO_FORWARD();
        sendKey(frontend, uuidAuto, FcitxKey_Escape);

        frontend->call<ITestFrontend::destroyInputContext>(uuidAuto);
    }

    // ====== Config: Whitelist Mode ======
    std::cerr << "\n=== Config: Whitelist Mode ===\n";

    {
        RawConfig cfg;
        cfg.setValueByPath("General/EnabledByDefault", "True");
        cfg.setValueByPath("General/ToggleKey/0", "Control+Escape");
        cfg.setValueByPath("AppFilter/Mode", "Whitelist");
        cfg.setValueByPath("AppFilter/Whitelist/0", "kitty");
        cfg.setValueByPath("AppFilter/Whitelist/1", "konsole");
        cfg.setValueByPath("Mappings/TimeoutMs", "200");
        cfg.setValueByPath("Mappings/InsertMap/0", "jk=Escape");
        vimod->setConfig(cfg);

        // App in Whitelist
        auto uuidKitty =
            frontend->call<ITestFrontend::createInputContext>("kitty");
        auto *icKitty = instance->inputContextManager().findByUUID(uuidKitty);
        if (icKitty) {
            icKitty->focusIn();
        }

        TEST("Whitelisted (kitty): EnabledByDefault aktiv -> h -> Left");
        sendKey(frontend, uuidKitty, FcitxKey_h);
        EXPECT_FORWARDED(FcitxKey_Left);

        frontend->call<ITestFrontend::destroyInputContext>(uuidKitty);

        // App nicht in Whitelist
        auto uuidFox =
            frontend->call<ITestFrontend::createInputContext>("firefox");
        auto *icFox = instance->inputContextManager().findByUUID(uuidFox);
        if (icFox) {
            icFox->focusIn();
        }

        TEST("Nicht-whitelisted (firefox): h geht durch");
        EXPECT_FALSE(sendKeyAccepted(frontend, uuidFox, FcitxKey_h));

        TEST("Nicht-whitelisted: Ctrl+Escape ebenfalls durch");
        EXPECT_FALSE(sendKeyAccepted(frontend, uuidFox,
                                     FcitxKey_Escape, KeyState::Ctrl));

        frontend->call<ITestFrontend::destroyInputContext>(uuidFox);
    }

    // ====== Config: Insert-Mode Mappings (jk -> Escape) ======
    std::cerr << "\n=== Config: Insert-Mode Mappings (jk -> Escape) ===\n";

    {
        // Default-Config wiederherstellen + EnabledByDefault aus
        RawConfig cfg;
        cfg.setValueByPath("General/EnabledByDefault", "False");
        cfg.setValueByPath("General/ToggleKey/0", "Control+Escape");
        cfg.setValueByPath("AppFilter/Mode", "None");
        cfg.setValueByPath("Mappings/TimeoutMs", "200");
        cfg.setValueByPath("Mappings/InsertMap/0", "jk=Escape");
        cfg.setValueByPath("Mappings/InsertMap/1", "jj=Return");
        vimod->setConfig(cfg);

        auto uuidM = frontend->call<ITestFrontend::createInputContext>("");
        auto *icM = instance->inputContextManager().findByUUID(uuidM);
        if (icM) {
            icM->focusIn();
        }

        // Aktivieren und in Insert-Mode wechseln
        sendKey(frontend, uuidM, FcitxKey_Escape, KeyState::Ctrl);
        sendKey(frontend, uuidM, FcitxKey_i);

        TEST("Insert: 'j' wird gepuffert (kein Forward, accepted)");
        EXPECT_TRUE(sendKeyAccepted(frontend, uuidM, FcitxKey_j));

        TEST("Insert: 'j' wurde nicht weitergeleitet");
        EXPECT_NO_FORWARD();
        // Note: forwarded ist von vorigem sendKeyAccepted geleert

        TEST("Insert: 'k' nach 'j' triggert jk-Mapping (kein Escape-Forward)");
        forwarded.clear();
        frontend->call<ITestFrontend::keyEvent>(uuidM, Key(FcitxKey_k), false);
        EXPECT_NO_FORWARD();

        TEST("Nach jk: zurueck in Normal Mode (h -> Left)");
        sendKey(frontend, uuidM, FcitxKey_h);
        EXPECT_FORWARDED(FcitxKey_Left);

        // jj -> Return Test (mapped to actual key, should forward)
        sendKey(frontend, uuidM, FcitxKey_i);
        TEST("Insert: 'j' gepuffert (zweiter Test)");
        EXPECT_TRUE(sendKeyAccepted(frontend, uuidM, FcitxKey_j));

        TEST("Insert: zweites 'j' loest jj-Mapping aus -> Return forwarded");
        forwarded.clear();
        frontend->call<ITestFrontend::keyEvent>(uuidM, Key(FcitxKey_j), false);
        EXPECT_FORWARDED(FcitxKey_Return);

        // Mode bleibt Insert (jj -> Return ist Forward, kein Escape)
        sendKey(frontend, uuidM, FcitxKey_Escape);

        // Flush-Test: 'j' gefolgt von nicht-passendem 'a'
        sendKey(frontend, uuidM, FcitxKey_i);
        forwarded.clear();
        frontend->call<ITestFrontend::keyEvent>(uuidM, Key(FcitxKey_j), false);

        TEST("Insert: 'j' + 'a' (no match) flusht 'j' und leitet 'a' durch");
        bool aAccepted = frontend->call<ITestFrontend::sendKeyEvent>(
            uuidM, Key(FcitxKey_a), false);
        // 'j' wurde geflusht (1 Forward), 'a' geht durch (nicht accepted)
        EXPECT_FORWARDED_FRONT(FcitxKey_j);
        TEST("Insert: 'a' wurde nicht von vimotion akzeptiert");
        EXPECT_FALSE(aAccepted);

        TEST("Insert: nach Flush nur 1 Forward (das geflushte 'j')");
        EXPECT_FORWARDED_COUNT(1);

        sendKey(frontend, uuidM, FcitxKey_Escape);

        // Pass-through wenn keine Sequence: 'x' ist kein Prefix
        sendKey(frontend, uuidM, FcitxKey_i);
        TEST("Insert: 'x' (kein Prefix) wird nicht akzeptiert");
        EXPECT_FALSE(sendKeyAccepted(frontend, uuidM, FcitxKey_x));

        TEST("Insert: 'x' produziert keinen Forward");
        EXPECT_NO_FORWARD();

        frontend->call<ITestFrontend::destroyInputContext>(uuidM);
    }

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
        {"module"},
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
