#include "vimotion.h"
#include "motions.h"
#include <algorithm>
#include <cstdint>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>

namespace vimotion {

namespace {

constexpr const char *kConfigFile = "conf/vimotion.conf";

// Trim leading/trailing whitespace
std::string trim(const std::string &s) {
    size_t a = 0;
    while (a < s.size() &&
           (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) {
        ++a;
    }
    size_t b = s.size();
    while (b > a &&
           (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' ||
            s[b - 1] == '\n')) {
        --b;
    }
    return s.substr(a, b - a);
}

} // namespace

Vimotion::Vimotion(fcitx::Instance *instance)
    : instance_(instance) {
    instance_->inputContextManager().registerProperty("vimotion-state",
                                                      &factory_);

    loadConfigFromFile();

    keyHandler_ = instance_->watchEvent(
        fcitx::EventType::InputContextKeyEvent,
        fcitx::EventWatcherPhase::PreInputMethod,
        [this](fcitx::Event &event) { handleKeyEvent(event); });

    // Auto-enable per InputContext when it gets focus
    focusInHandler_ = instance_->watchEvent(
        fcitx::EventType::InputContextFocusIn,
        fcitx::EventWatcherPhase::PostInputMethod,
        [this](fcitx::Event &event) {
            auto &icEvent = static_cast<fcitx::InputContextEvent &>(event);
            auto *ic = icEvent.inputContext();
            auto *state = ic->propertyFor(&factory_);
            if (enabledByDefault_ && !state->enabled && !isFiltered(ic)) {
                state->enabled = true;
                state->mode = Mode::Normal;
                resetState(state);
                updateModeDisplay(state, ic);
            }
        });

    // Clear pending sequence buffer when focus leaves the IC so a stale
    // timer cannot reach a destroyed input context.
    focusOutHandler_ = instance_->watchEvent(
        fcitx::EventType::InputContextFocusOut,
        fcitx::EventWatcherPhase::PostInputMethod,
        [this](fcitx::Event &event) {
            auto &icEvent = static_cast<fcitx::InputContextEvent &>(event);
            auto *state = icEvent.inputContext()->propertyFor(&factory_);
            clearSeqBuffer(state);
        });
}

void Vimotion::loadConfigFromFile() {
    fcitx::readAsIni(config_, kConfigFile);
    applyConfig();
}

void Vimotion::setConfig(const fcitx::RawConfig &rawConfig) {
    config_.load(rawConfig);
    fcitx::safeSaveAsIni(config_, kConfigFile);
    applyConfig();
}

void Vimotion::reloadConfig() {
    loadConfigFromFile();
}

void Vimotion::applyConfig() {
    enabledByDefault_ = *config_.general->enabledByDefault;
    toggleKeys_ = *config_.general->toggleKey;
    filterMode_ = *config_.appFilter->mode;
    blacklist_ = *config_.appFilter->blacklist;
    whitelist_ = *config_.appFilter->whitelist;
    seqTimeoutMs_ = *config_.mappings->timeoutMs;
    parseInsertMappings();
}

void Vimotion::parseInsertMappings() {
    insertMappings_.clear();
    for (const auto &line : *config_.mappings->insertMap) {
        auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        auto eq = trimmed.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 >= trimmed.size()) {
            continue;
        }
        std::string from = trim(trimmed.substr(0, eq));
        std::string to = trim(trimmed.substr(eq + 1));
        if (from.empty() || to.empty()) continue;

        // Reject sequences that contain non-printable characters; they cannot
        // be matched by typed input anyway.
        bool ok = true;
        for (char c : from) {
            if (static_cast<unsigned char>(c) < 0x20 ||
                static_cast<unsigned char>(c) > 0x7E) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;

        fcitx::Key target(to);
        if (target.sym() == FcitxKey_None) continue;
        insertMappings_.push_back({std::move(from), target});
    }
}

void Vimotion::handleKeyEvent(fcitx::Event &event) {
    auto &keyEvent = static_cast<fcitx::KeyEvent &>(event);

    if (keyEvent.isRelease()) {
        return;
    }

    auto *ic = keyEvent.inputContext();
    auto *state = ic->propertyFor(&factory_);

    // App-Filter (Black-/Whitelist)
    if (isFiltered(ic)) {
        return;
    }

    // Toggle-Hotkey
    if (keyEvent.key().checkKeyList(toggleKeys_)) {
        state->enabled = !state->enabled;
        if (state->enabled) {
            state->mode = Mode::Normal;
            resetState(state);
        } else {
            clearSeqBuffer(state);
            resetState(state);
        }
        updateModeDisplay(state, ic);
        keyEvent.filterAndAccept();
        return;
    }

    // Wenn nicht aktiviert, alles durchlassen
    if (!state->enabled) {
        return;
    }

    switch (state->mode) {
    case Mode::Normal:
        handleNormalMode(state, keyEvent);
        break;
    case Mode::Insert:
        handleInsertMode(state, keyEvent);
        break;
    case Mode::OperatorPending:
        handleOperatorPending(state, keyEvent);
        break;
    }
}

bool Vimotion::isPrintableAscii(fcitx::KeySym sym) const {
    return sym >= 0x20 && sym <= 0x7E;
}

void Vimotion::clearSeqBuffer(VimState *state) {
    state->seqText.clear();
    state->seqKeys.clear();
    state->seqTimer.reset();
}

void Vimotion::flushSeqBuffer(VimState *state, fcitx::InputContext *ic) {
    auto keys = std::move(state->seqKeys);
    state->seqText.clear();
    state->seqTimer.reset();
    for (const auto &k : keys) {
        ic->forwardKey(k);
    }
}

void Vimotion::scheduleSeqTimeout(VimState *state,
                                        fcitx::InputContext *ic) {
    auto &loop = instance_->eventLoop();
    uint64_t now = fcitx::now(CLOCK_MONOTONIC);
    state->seqTimer = loop.addTimeEvent(
        CLOCK_MONOTONIC,
        now + static_cast<uint64_t>(seqTimeoutMs_) * 1000ULL,
        0,
        [this, state, ic](fcitx::EventSourceTime *, uint64_t) {
            if (!state->seqText.empty()) {
                flushSeqBuffer(state, ic);
            }
            return true;
        });
}

void Vimotion::handleInsertMode(VimState *state,
                                      fcitx::KeyEvent &event) {
    auto *ic = event.inputContext();

    // Escape always returns to Normal mode (and flushes any pending seq).
    if (event.key().check(FcitxKey_Escape)) {
        if (!state->seqText.empty()) {
            // Forward buffered keys before mode change so the user does not
            // lose what they typed.
            flushSeqBuffer(state, ic);
        }
        switchMode(state, Mode::Normal, ic);
        event.filterAndAccept();
        return;
    }

    // Modified keys (Ctrl/Alt/Super) bypass the sequence buffer entirely.
    if (event.key().states() &
        fcitx::KeyStates({fcitx::KeyState::Ctrl, fcitx::KeyState::Alt,
                          fcitx::KeyState::Super})) {
        if (!state->seqText.empty()) {
            flushSeqBuffer(state, ic);
        }
        return;
    }

    // Only printable ASCII keys feed into the sequence matcher.
    if (!isPrintableAscii(event.key().sym())) {
        if (!state->seqText.empty()) {
            flushSeqBuffer(state, ic);
        }
        return;
    }

    if (insertMappings_.empty()) {
        return; // Nothing to match against.
    }

    char ch = static_cast<char>(event.key().sym());
    std::string candidate = state->seqText + ch;

    // Look for an exact match first.
    for (const auto &m : insertMappings_) {
        if (m.from == candidate) {
            clearSeqBuffer(state);
            // Mapping the sequence to Escape mirrors the user pressing Escape
            // directly: switch back to Normal mode instead of forwarding
            // Escape to the underlying application.
            if (m.target.check(FcitxKey_Escape)) {
                switchMode(state, Mode::Normal, ic);
            } else {
                ic->forwardKey(m.target);
            }
            event.filterAndAccept();
            return;
        }
    }

    // Look for a prefix match (potential longer sequence).
    bool isPrefix = false;
    for (const auto &m : insertMappings_) {
        if (m.from.size() > candidate.size() &&
            m.from.compare(0, candidate.size(), candidate) == 0) {
            isPrefix = true;
            break;
        }
    }

    if (isPrefix) {
        state->seqText = candidate;
        state->seqKeys.push_back(event.key());
        scheduleSeqTimeout(state, ic);
        event.filterAndAccept();
        return;
    }

    // No match and no prefix: flush whatever is buffered, then let the new
    // key pass through unchanged.
    if (!state->seqText.empty()) {
        flushSeqBuffer(state, ic);
    }
    // Let key pass through to the underlying IM / application.
}

void Vimotion::handleNormalMode(VimState *state,
                                      fcitx::KeyEvent &event) {
    auto key = event.key();
    auto *ic = event.inputContext();

    // Ctrl+R: Redo
    if (key.check(FcitxKey_r, fcitx::KeyState::Ctrl) ||
        key.check(FcitxKey_R, fcitx::KeyState::Ctrl)) {
        int effectiveCount = state->countActive ? state->count : 1;
        for (int i = 0; i < effectiveCount; ++i) {
            ic->forwardKey(fcitx::Key(FcitxKey_y, fcitx::KeyState::Ctrl));
        }
        resetState(state);
        event.filterAndAccept();
        return;
    }

    // Modifizierte Tasten (Ctrl/Alt/Super) durchlassen
    if (key.states() & fcitx::KeyStates({fcitx::KeyState::Ctrl,
                                         fcitx::KeyState::Alt,
                                         fcitx::KeyState::Super})) {
        return;
    }

    int effectiveCount = state->countActive ? state->count : 1;

    // Pending-g Sequenz: gg -> Dokumentanfang
    if (state->pendingG) {
        state->pendingG = false;
        if (key.check(FcitxKey_g)) {
            executeMotion(ic, fcitx::Key(FcitxKey_Home, fcitx::KeyState::Ctrl),
                          1);
            resetState(state);
            event.filterAndAccept();
            return;
        }
        resetState(state);
        event.filterAndAccept();
        return;
    }

    if (key.check(FcitxKey_g)) {
        state->pendingG = true;
        event.filterAndAccept();
        return;
    }

    if (key.check(FcitxKey_e)) {
        executeMotionE(ic, effectiveCount, false);
        resetState(state);
        event.filterAndAccept();
        return;
    }

    for (const auto &m : getMotionMappings()) {
        if (key.check(m.vimKey)) {
            if (m.vimKey == FcitxKey_0 && state->countActive) {
                state->count = state->count * 10;
                event.filterAndAccept();
                return;
            }
            executeMotion(ic, m.forwardKey, effectiveCount);
            resetState(state);
            event.filterAndAccept();
            return;
        }
    }

    if (key.sym() >= FcitxKey_1 && key.sym() <= FcitxKey_9) {
        int digit = key.sym() - FcitxKey_0;
        if (state->countActive) {
            state->count = state->count * 10 + digit;
        } else {
            state->count = digit;
            state->countActive = true;
        }
        updateModeDisplay(state, ic);
        event.filterAndAccept();
        return;
    }

    if (key.check(FcitxKey_i)) {
        switchMode(state, Mode::Insert, ic);
        resetState(state);
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_a)) {
        ic->forwardKey(fcitx::Key(FcitxKey_Right));
        switchMode(state, Mode::Insert, ic);
        resetState(state);
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_I)) {
        ic->forwardKey(fcitx::Key(FcitxKey_Home));
        switchMode(state, Mode::Insert, ic);
        resetState(state);
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_A)) {
        ic->forwardKey(fcitx::Key(FcitxKey_End));
        switchMode(state, Mode::Insert, ic);
        resetState(state);
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_o)) {
        ic->forwardKey(fcitx::Key(FcitxKey_End));
        ic->forwardKey(fcitx::Key(FcitxKey_Return));
        switchMode(state, Mode::Insert, ic);
        resetState(state);
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_O)) {
        ic->forwardKey(fcitx::Key(FcitxKey_Home));
        ic->forwardKey(fcitx::Key(FcitxKey_Return));
        ic->forwardKey(fcitx::Key(FcitxKey_Up));
        switchMode(state, Mode::Insert, ic);
        resetState(state);
        event.filterAndAccept();
        return;
    }

    if (key.check(FcitxKey_x)) {
        for (int i = 0; i < effectiveCount; ++i) {
            ic->forwardKey(fcitx::Key(FcitxKey_Delete));
        }
        resetState(state);
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_X)) {
        for (int i = 0; i < effectiveCount; ++i) {
            ic->forwardKey(fcitx::Key(FcitxKey_BackSpace));
        }
        resetState(state);
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_p)) {
        ic->forwardKey(fcitx::Key(FcitxKey_End));
        ic->forwardKey(fcitx::Key(FcitxKey_Return));
        ic->forwardKey(pasteKey(ic));
        resetState(state);
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_P)) {
        ic->forwardKey(fcitx::Key(FcitxKey_Home));
        ic->forwardKey(fcitx::Key(FcitxKey_Return));
        ic->forwardKey(fcitx::Key(FcitxKey_Up));
        ic->forwardKey(pasteKey(ic));
        resetState(state);
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_u)) {
        for (int i = 0; i < effectiveCount; ++i) {
            ic->forwardKey(fcitx::Key(FcitxKey_z, fcitx::KeyState::Ctrl));
        }
        resetState(state);
        event.filterAndAccept();
        return;
    }

    if (key.check(FcitxKey_d)) {
        state->pendingOp = Operator::Delete;
        switchMode(state, Mode::OperatorPending, ic);
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_y)) {
        state->pendingOp = Operator::Yank;
        switchMode(state, Mode::OperatorPending, ic);
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_c)) {
        state->pendingOp = Operator::Change;
        switchMode(state, Mode::OperatorPending, ic);
        event.filterAndAccept();
        return;
    }

    if (key.check(FcitxKey_Escape)) {
        resetState(state);
        updateModeDisplay(state, ic);
        event.filterAndAccept();
        return;
    }

    // Druckbare Zeichen konsumieren (kein Tippen im Normal Mode).
    if (key.sym() >= 0x20 && key.sym() <= 0x7E) {
        event.filterAndAccept();
    }
}

void Vimotion::handleOperatorPending(VimState *state,
                                           fcitx::KeyEvent &event) {
    auto key = event.key();
    auto *ic = event.inputContext();

    if (key.check(FcitxKey_Escape)) {
        switchMode(state, Mode::Normal, ic);
        resetState(state);
        event.filterAndAccept();
        return;
    }

    int effectiveCount = state->countActive ? state->count : 1;

    if (key.sym() >= FcitxKey_1 && key.sym() <= FcitxKey_9) {
        int digit = key.sym() - FcitxKey_0;
        if (state->countActive) {
            state->count = state->count * 10 + digit;
        } else {
            state->count = digit;
            state->countActive = true;
        }
        updateModeDisplay(state, ic);
        event.filterAndAccept();
        return;
    }

    if ((state->pendingOp == Operator::Delete && key.check(FcitxKey_d)) ||
        (state->pendingOp == Operator::Yank && key.check(FcitxKey_y)) ||
        (state->pendingOp == Operator::Change && key.check(FcitxKey_c))) {
        executeLineOperator(ic, state, state->pendingOp, effectiveCount);
        switchMode(state, Mode::Normal, ic);
        resetState(state);
        event.filterAndAccept();
        return;
    }

    if (state->pendingG) {
        state->pendingG = false;
        if (key.check(FcitxKey_g)) {
            executeOperator(ic, state, state->pendingOp,
                            fcitx::Key(FcitxKey_Home,
                                       fcitx::KeyState::Ctrl_Shift),
                            1);
            if (state->pendingOp != Operator::Change) {
                switchMode(state, Mode::Normal, ic);
            }
            resetState(state);
            event.filterAndAccept();
            return;
        }
        switchMode(state, Mode::Normal, ic);
        resetState(state);
        event.filterAndAccept();
        return;
    }

    if (key.check(FcitxKey_g)) {
        state->pendingG = true;
        event.filterAndAccept();
        return;
    }

    if (key.check(FcitxKey_e)) {
        executeMotionE(ic, effectiveCount, true);
        switch (state->pendingOp) {
        case Operator::Delete:
            ic->forwardKey(fcitx::Key(FcitxKey_Delete));
            break;
        case Operator::Yank:
            ic->forwardKey(copyKey(ic));
            ic->forwardKey(fcitx::Key(FcitxKey_Right));
            ic->forwardKey(fcitx::Key(FcitxKey_Left));
            break;
        case Operator::Change:
            ic->forwardKey(fcitx::Key(FcitxKey_Delete));
            switchMode(state, Mode::Insert, ic);
            break;
        case Operator::None:
            break;
        }
        if (state->pendingOp != Operator::Change) {
            switchMode(state, Mode::Normal, ic);
        }
        resetState(state);
        event.filterAndAccept();
        return;
    }

    for (const auto &m : getMotionMappings()) {
        if (key.check(m.vimKey)) {
            if (m.vimKey == FcitxKey_0 && state->countActive) {
                state->count = state->count * 10;
                event.filterAndAccept();
                return;
            }
            executeOperator(ic, state, state->pendingOp, m.shiftVariant,
                            effectiveCount);
            if (state->pendingOp != Operator::Change) {
                switchMode(state, Mode::Normal, ic);
            }
            resetState(state);
            event.filterAndAccept();
            return;
        }
    }

    switchMode(state, Mode::Normal, ic);
    resetState(state);
    event.filterAndAccept();
}

void Vimotion::executeMotion(fcitx::InputContext *ic, fcitx::Key motion,
                                   int count) {
    for (int i = 0; i < count; ++i) {
        ic->forwardKey(motion);
    }
}

void Vimotion::executeMotionE(fcitx::InputContext *ic, int count,
                                    bool withShift) {
    auto ctrlRight = withShift
        ? fcitx::Key(FcitxKey_Right, fcitx::KeyState::Ctrl_Shift)
        : fcitx::Key(FcitxKey_Right, fcitx::KeyState::Ctrl);
    auto left = withShift
        ? fcitx::Key(FcitxKey_Left, fcitx::KeyState::Shift)
        : fcitx::Key(FcitxKey_Left);

    for (int i = 0; i < count; ++i) {
        ic->forwardKey(ctrlRight);
    }
    ic->forwardKey(left);
}

void Vimotion::executeOperator(fcitx::InputContext *ic, VimState *state,
                                     Operator op, fcitx::Key shiftMotion,
                                     int count) {
    for (int i = 0; i < count; ++i) {
        ic->forwardKey(shiftMotion);
    }

    switch (op) {
    case Operator::Delete:
        ic->forwardKey(fcitx::Key(FcitxKey_Delete));
        break;
    case Operator::Yank:
        ic->forwardKey(copyKey(ic));
        ic->forwardKey(fcitx::Key(FcitxKey_Right));
        ic->forwardKey(fcitx::Key(FcitxKey_Left));
        break;
    case Operator::Change:
        ic->forwardKey(fcitx::Key(FcitxKey_Delete));
        switchMode(state, Mode::Insert, ic);
        break;
    case Operator::None:
        break;
    }
}

void Vimotion::executeLineOperator(fcitx::InputContext *ic,
                                         VimState *state, Operator op,
                                         int count) {
    ic->forwardKey(fcitx::Key(FcitxKey_Home));
    for (int i = 0; i < count; ++i) {
        ic->forwardKey(fcitx::Key(FcitxKey_Down, fcitx::KeyState::Shift));
    }

    switch (op) {
    case Operator::Delete:
        ic->forwardKey(fcitx::Key(FcitxKey_Delete));
        break;
    case Operator::Yank:
        ic->forwardKey(copyKey(ic));
        ic->forwardKey(fcitx::Key(FcitxKey_Left));
        break;
    case Operator::Change:
        ic->forwardKey(fcitx::Key(FcitxKey_Home));
        ic->forwardKey(fcitx::Key(FcitxKey_End, fcitx::KeyState::Shift));
        ic->forwardKey(fcitx::Key(FcitxKey_Delete));
        switchMode(state, Mode::Insert, ic);
        break;
    case Operator::None:
        break;
    }
}

void Vimotion::switchMode(VimState *state, Mode newMode,
                                fcitx::InputContext *ic) {
    state->mode = newMode;
    if (newMode != Mode::Insert) {
        // Sequence buffering only matters in Insert mode.
        clearSeqBuffer(state);
    }
    updateModeDisplay(state, ic);
}

void Vimotion::updateModeDisplay(VimState *state,
                                       fcitx::InputContext *ic) {
    auto &inputPanel = ic->inputPanel();
    inputPanel.reset();

    fcitx::Text aux;
    if (state->enabled) {
        switch (state->mode) {
        case Mode::Normal: {
            std::string display = "[N]";
            if (state->countActive) {
                display += " " + std::to_string(state->count);
            }
            aux.append(display);
            break;
        }
        case Mode::OperatorPending: {
            std::string display = "[N] ";
            if (state->countActive) {
                display += std::to_string(state->count);
            }
            switch (state->pendingOp) {
            case Operator::Delete: display += "d"; break;
            case Operator::Yank:   display += "y"; break;
            case Operator::Change: display += "c"; break;
            case Operator::None: break;
            }
            if (state->pendingG) {
                display += "g";
            }
            aux.append(display);
            break;
        }
        case Mode::Insert:
            aux.append("[I]");
            break;
        }
    }

    inputPanel.setAuxUp(aux);
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void Vimotion::resetState(VimState *state) {
    state->pendingOp = Operator::None;
    state->count = 0;
    state->countActive = false;
    state->pendingG = false;
}

bool Vimotion::isFiltered(fcitx::InputContext *ic) const {
    if (filterMode_ == AppFilterMode::None) return false;

    const std::string &program = ic->program();
    if (program.empty()) return false;

    if (filterMode_ == AppFilterMode::Blacklist) {
        for (const auto &app : blacklist_) {
            if (!app.empty() && program.find(app) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    // Whitelist: only active in listed apps
    for (const auto &app : whitelist_) {
        if (!app.empty() && program.find(app) != std::string::npos) {
            return false;
        }
    }
    return true;
}

bool Vimotion::isTerminal(fcitx::InputContext *ic) const {
    const auto &program = ic->program();
    static const std::vector<std::string> terminals = {
        "wezterm", "kitty", "alacritty", "ghostty", "xterm",
        "gnome-terminal", "konsole", "foot", "st",
        "urxvt", "termite", "tilix", "sakura"
    };
    for (const auto &t : terminals) {
        if (program.find(t) != std::string::npos) {
            return true;
        }
    }
    return false;
}

fcitx::Key Vimotion::copyKey(fcitx::InputContext *ic) const {
    if (isTerminal(ic)) {
        return fcitx::Key(FcitxKey_c, fcitx::KeyState::Ctrl_Shift);
    }
    return fcitx::Key(FcitxKey_c, fcitx::KeyState::Ctrl);
}

fcitx::Key Vimotion::pasteKey(fcitx::InputContext *ic) const {
    if (isTerminal(ic)) {
        return fcitx::Key(FcitxKey_v, fcitx::KeyState::Ctrl_Shift);
    }
    return fcitx::Key(FcitxKey_v, fcitx::KeyState::Ctrl);
}

fcitx::AddonInstance *
VimotionFactory::create(fcitx::AddonManager *manager) {
    return new Vimotion(manager->instance());
}

} // namespace vimotion

FCITX_ADDON_FACTORY(vimotion::VimotionFactory);
