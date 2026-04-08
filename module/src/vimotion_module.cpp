#include "vimotion_module.h"
#include "motions.h"
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>

// Motions aus der gemeinsamen Tabelle (addon/src/motions.cpp)
using vimotion::getMotionMappings;

namespace vimotion_module {

VimotionModule::VimotionModule(fcitx::Instance *instance)
    : instance_(instance) {
    // Per-IC State registrieren
    instance_->inputContextManager().registerProperty("vimotion-state",
                                                      &factory_);

    // Key-Handler in PreInputMethod Phase registrieren
    keyHandler_ = instance_->watchEvent(
        fcitx::EventType::InputContextKeyEvent,
        fcitx::EventWatcherPhase::PreInputMethod,
        [this](fcitx::Event &event) { handleKeyEvent(event); });
}

void VimotionModule::handleKeyEvent(fcitx::Event &event) {
    auto &keyEvent = static_cast<fcitx::KeyEvent &>(event);

    if (keyEvent.isRelease()) {
        return;
    }

    auto *ic = keyEvent.inputContext();
    auto *state = ic->propertyFor(&factory_);

    // Blacklist pruefen
    if (isBlacklisted(ic)) {
        return;
    }

    // Toggle-Hotkey: Ctrl+Escape
    if (keyEvent.key().check(toggleKey_)) {
        state->enabled = !state->enabled;
        if (state->enabled) {
            state->mode = Mode::Normal;
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

void VimotionModule::handleInsertMode(VimState *state,
                                      fcitx::KeyEvent &event) {
    if (event.key().check(FcitxKey_Escape)) {
        switchMode(state, Mode::Normal, event.inputContext());
        event.filterAndAccept();
        return;
    }
    // Alle anderen Tasten durchlassen (-> an schnelle-umlaute / IM)
}

void VimotionModule::handleNormalMode(VimState *state,
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

    // g-Taste: Pending setzen
    if (key.check(FcitxKey_g)) {
        state->pendingG = true;
        event.filterAndAccept();
        return;
    }

    // e-Motion: Wortende (Ctrl+Right, Left)
    if (key.check(FcitxKey_e)) {
        executeMotionE(ic, effectiveCount, false);
        resetState(state);
        event.filterAndAccept();
        return;
    }

    // Motion-Lookup ueber Mapping-Tabelle
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

    // Count-Prefix: Ziffern 1-9 starten, 0 setzt fort
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

    // Insert-Mode Einstiege
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

    // Einfache Befehle
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

    // Operatoren -> Operator-Pending
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

    // Escape: State zuruecksetzen
    if (key.check(FcitxKey_Escape)) {
        resetState(state);
        updateModeDisplay(state, ic);
        event.filterAndAccept();
        return;
    }

    // Druckbare Zeichen konsumieren (kein Tippen im Normal Mode)
    // Alles andere (F-Tasten, Pfeiltasten, Backspace, etc.) durchlassen
    if (key.sym() >= 0x20 && key.sym() <= 0x7E) {
        event.filterAndAccept();
    }
}

void VimotionModule::handleOperatorPending(VimState *state,
                                           fcitx::KeyEvent &event) {
    auto key = event.key();
    auto *ic = event.inputContext();

    // Escape: zurueck zu Normal
    if (key.check(FcitxKey_Escape)) {
        switchMode(state, Mode::Normal, ic);
        resetState(state);
        event.filterAndAccept();
        return;
    }

    int effectiveCount = state->countActive ? state->count : 1;

    // Count im Operator-Pending
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

    // Gleicher Operator nochmal -> Zeilenoperator (dd/yy/cc)
    if ((state->pendingOp == Operator::Delete && key.check(FcitxKey_d)) ||
        (state->pendingOp == Operator::Yank && key.check(FcitxKey_y)) ||
        (state->pendingOp == Operator::Change && key.check(FcitxKey_c))) {
        executeLineOperator(ic, state, state->pendingOp, effectiveCount);
        switchMode(state, Mode::Normal, ic);
        resetState(state);
        event.filterAndAccept();
        return;
    }

    // Pending-g Sequenz: dgg, ygg, cgg
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

    // e-Motion mit Operator
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

    // Motion-Lookup: Operator + Motion ausfuehren
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

    // Unbekannte Taste: abbrechen
    switchMode(state, Mode::Normal, ic);
    resetState(state);
    event.filterAndAccept();
}

void VimotionModule::executeMotion(fcitx::InputContext *ic, fcitx::Key motion,
                                   int count) {
    for (int i = 0; i < count; ++i) {
        ic->forwardKey(motion);
    }
}

void VimotionModule::executeMotionE(fcitx::InputContext *ic, int count,
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

void VimotionModule::executeOperator(fcitx::InputContext *ic, VimState *state,
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

void VimotionModule::executeLineOperator(fcitx::InputContext *ic,
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

void VimotionModule::switchMode(VimState *state, Mode newMode,
                                fcitx::InputContext *ic) {
    state->mode = newMode;
    updateModeDisplay(state, ic);
}

void VimotionModule::updateModeDisplay(VimState *state,
                                       fcitx::InputContext *ic) {
    auto &inputPanel = ic->inputPanel();
    inputPanel.reset();

    fcitx::Text aux;
    if (!state->enabled) {
        // Nichts anzeigen wenn deaktiviert
    } else {
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

void VimotionModule::resetState(VimState *state) {
    state->pendingOp = Operator::None;
    state->count = 0;
    state->countActive = false;
    state->pendingG = false;
}

bool VimotionModule::isBlacklisted(fcitx::InputContext *ic) const {
    const auto &program = ic->program();
    for (const auto &app : blacklist_) {
        if (program.find(app) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool VimotionModule::isTerminal(fcitx::InputContext *ic) const {
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

fcitx::Key VimotionModule::copyKey(fcitx::InputContext *ic) const {
    if (isTerminal(ic)) {
        return fcitx::Key(FcitxKey_c, fcitx::KeyState::Ctrl_Shift);
    }
    return fcitx::Key(FcitxKey_c, fcitx::KeyState::Ctrl);
}

fcitx::Key VimotionModule::pasteKey(fcitx::InputContext *ic) const {
    if (isTerminal(ic)) {
        return fcitx::Key(FcitxKey_v, fcitx::KeyState::Ctrl_Shift);
    }
    return fcitx::Key(FcitxKey_v, fcitx::KeyState::Ctrl);
}

fcitx::AddonInstance *
VimotionModuleFactory::create(fcitx::AddonManager *manager) {
    return new VimotionModule(manager->instance());
}

} // namespace vimotion_module

FCITX_ADDON_FACTORY(vimotion_module::VimotionModuleFactory);
