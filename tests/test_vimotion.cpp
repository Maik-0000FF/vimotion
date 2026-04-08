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

static void sendKey(AddonInstance *frontend, const ICUUID &uuid,
                    KeySym sym, KeyStates states = KeyState::NoState) {
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(sym, states), false);
}

static void runTests(Instance *instance) {
    auto *frontend = instance->addonManager().addon("testfrontend");
    if (!frontend) {
        std::cerr << "FATAL: testfrontend nicht geladen\n";
        instance->exit();
        return;
    }

    // Vimotion Addon laden
    auto *vimAddon = instance->addonManager().addon("vimotion", true);
    if (!vimAddon) {
        std::cerr << "FATAL: vimotion addon nicht geladen\n";
        instance->exit();
        return;
    }

    auto &imManager = instance->inputMethodManager();
    // IM-Name leitet sich vom Dateinamen in inputmethod/ ab
    std::string vimImName = "vimotion-im";

    // Default-Gruppe aendern statt neue erstellen
    auto group = imManager.currentGroup();
    group.inputMethodList().clear();
    group.inputMethodList().emplace_back(vimImName);
    group.setDefaultInputMethod(vimImName);
    imManager.setGroup(std::move(group));
    imManager.setDefaultInputMethod(vimImName);

    // InputContext NACH der Group-Konfiguration erstellen
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

    // ====== Normal Mode: Basis-Motions ======
    std::cerr << "\n=== Normal Mode: Motions ===\n";

    TEST("h -> Left");
    sendKey(frontend, uuid, FcitxKey_h);
    EXPECT_FORWARDED(FcitxKey_Left);

    TEST("l -> Right");
    sendKey(frontend, uuid, FcitxKey_l);
    EXPECT_FORWARDED(FcitxKey_Right);

    TEST("j -> Down");
    sendKey(frontend, uuid, FcitxKey_j);
    EXPECT_FORWARDED(FcitxKey_Down);

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

    // ====== e-Motion ======
    TEST("e -> Ctrl+Right, Left (2 keys)");
    sendKey(frontend, uuid, FcitxKey_e);
    EXPECT_FORWARDED_COUNT(2);

    // ====== gg-Sequenz ======
    std::cerr << "\n=== gg Sequenz ===\n";

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

    TEST("2w -> 2x Ctrl+Right");
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_2), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_w), false);
    EXPECT_FORWARDED_COUNT(2);

    // ====== Insert Mode ======
    std::cerr << "\n=== Insert Mode ===\n";

    TEST("i -> Insert Mode (kein Forward)");
    sendKey(frontend, uuid, FcitxKey_i);
    EXPECT_NO_FORWARD();

    TEST("Insert Mode: normale Taste wird durchgelassen");
    sendKey(frontend, uuid, FcitxKey_x);
    // In Insert Mode sollte x nicht konsumiert werden (kein Forward via Engine)
    EXPECT_NO_FORWARD();

    TEST("Escape -> zurueck zu Normal");
    sendKey(frontend, uuid, FcitxKey_Escape);
    EXPECT_NO_FORWARD();

    // Pruefen dass wir wieder in Normal sind
    TEST("h nach Escape -> Left (wieder Normal)");
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

    // ====== Insert-Einstiege ======
    std::cerr << "\n=== Insert-Einstiege ===\n";

    TEST("a -> Right + Insert Mode");
    sendKey(frontend, uuid, FcitxKey_a);
    EXPECT_FORWARDED(FcitxKey_Right);
    // Zurueck zu Normal fuer naechsten Test
    sendKey(frontend, uuid, FcitxKey_Escape);

    TEST("A -> End + Insert Mode");
    sendKey(frontend, uuid, FcitxKey_A);
    EXPECT_FORWARDED(FcitxKey_End);
    sendKey(frontend, uuid, FcitxKey_Escape);

    TEST("I -> Home + Insert Mode");
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

    TEST("yy -> Home, Shift+Down, Ctrl+C, End");
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_y), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_y), false);
    EXPECT_FORWARDED_COUNT(4);

    TEST("cw -> Shift+Ctrl+Right, Delete (+ Insert Mode)");
    forwarded.clear();
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_c), false);
    frontend->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_w), false);
    EXPECT_FORWARDED_COUNT(2);
    // Sollte jetzt im Insert Mode sein
    sendKey(frontend, uuid, FcitxKey_Escape);

    // ====== Modifier durchlassen ======
    std::cerr << "\n=== Modifier Pass-through ===\n";

    TEST("Ctrl+S wird durchgelassen (kein Forward)");
    sendKey(frontend, uuid, FcitxKey_s, KeyState::Ctrl);
    EXPECT_NO_FORWARD();

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
        {"addon"},
        {"tests"});

    char arg0[] = "test_vimotion";
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
