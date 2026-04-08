#include "vimotion.h"
#include "motions.h"
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>

namespace vimotion {

VimotionEngine::VimotionEngine(fcitx::Instance *instance)
    : instance_(instance) {}

void VimotionEngine::activate(const fcitx::InputMethodEntry &,
                              fcitx::InputContextEvent &) {
    mode_ = Mode::Normal;
    resetState();
}

void VimotionEngine::deactivate(const fcitx::InputMethodEntry &,
                                fcitx::InputContextEvent &) {
    resetState();
}

void VimotionEngine::reset(const fcitx::InputMethodEntry &,
                           fcitx::InputContextEvent &) {
    resetState();
}

void VimotionEngine::keyEvent(const fcitx::InputMethodEntry &,
                              fcitx::KeyEvent &event) {
    if (event.isRelease()) {
        return;
    }

    switch (mode_) {
    case Mode::Normal:
        handleNormalMode(event);
        break;
    case Mode::Insert:
        handleInsertMode(event);
        break;
    case Mode::OperatorPending:
        handleOperatorPending(event);
        break;
    }
}

void VimotionEngine::handleInsertMode(fcitx::KeyEvent &event) {
    if (event.key().check(FcitxKey_Escape)) {
        switchMode(Mode::Normal, event.inputContext());
        event.filterAndAccept();
        return;
    }
    // Alle anderen Tasten durchlassen (transparente Eingabe)
}

void VimotionEngine::handleNormalMode(fcitx::KeyEvent &event) {
    auto key = event.key();
    auto *ic = event.inputContext();

    // Ctrl+R: Redo (Grossbuchstabe weil Ctrl den Sym normalisiert)
    if (key.check(FcitxKey_r, fcitx::KeyState::Ctrl) ||
        key.check(FcitxKey_R, fcitx::KeyState::Ctrl)) {
        int effectiveCount = countActive_ ? count_ : 1;
        for (int i = 0; i < effectiveCount; ++i) {
            ic->forwardKey(fcitx::Key(FcitxKey_y, fcitx::KeyState::Ctrl));
        }
        resetState();
        event.filterAndAccept();
        return;
    }

    // Modifizierte Tasten (Ctrl/Alt/Super) durchlassen
    if (key.states() & fcitx::KeyStates({fcitx::KeyState::Ctrl,
                                         fcitx::KeyState::Alt,
                                         fcitx::KeyState::Super})) {
        return;
    }

    int effectiveCount = countActive_ ? count_ : 1;

    // Pending-g Sequenz: gg -> Dokumentanfang
    if (pendingG_) {
        pendingG_ = false;
        if (key.check(FcitxKey_g)) {
            executeMotion(ic, fcitx::Key(FcitxKey_Home, fcitx::KeyState::Ctrl),
                          1);
            resetState();
            event.filterAndAccept();
            return;
        }
        // Kein g nachgefolgt: State zuruecksetzen, Taste konsumieren
        resetState();
        event.filterAndAccept();
        return;
    }

    // g-Taste: Pending setzen
    if (key.check(FcitxKey_g)) {
        pendingG_ = true;
        event.filterAndAccept();
        return;
    }

    // e-Motion: Wortende (Ctrl+Right, Left)
    if (key.check(FcitxKey_e)) {
        executeMotionE(ic, effectiveCount, false);
        resetState();
        event.filterAndAccept();
        return;
    }

    // Motion-Lookup ueber Mapping-Tabelle
    for (const auto &m : getMotionMappings()) {
        if (key.check(m.vimKey)) {
            // Sonderfall: 0 ist nur Motion wenn kein Count laeuft
            if (m.vimKey == FcitxKey_0 && countActive_) {
                count_ = count_ * 10;
                event.filterAndAccept();
                return;
            }
            executeMotion(ic, m.forwardKey, effectiveCount);
            resetState();
            event.filterAndAccept();
            return;
        }
    }

    // Count-Prefix: Ziffern 1-9 starten, 0 setzt fort (oben behandelt)
    if (key.sym() >= FcitxKey_1 && key.sym() <= FcitxKey_9) {
        int digit = key.sym() - FcitxKey_0;
        if (countActive_) {
            count_ = count_ * 10 + digit;
        } else {
            count_ = digit;
            countActive_ = true;
        }
        updateModeDisplay(ic);
        event.filterAndAccept();
        return;
    }

    // Insert-Mode Einstiege
    if (key.check(FcitxKey_i)) {
        switchMode(Mode::Insert, ic);
        resetState();
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_a)) {
        ic->forwardKey(fcitx::Key(FcitxKey_Right));
        switchMode(Mode::Insert, ic);
        resetState();
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_I)) {
        ic->forwardKey(fcitx::Key(FcitxKey_Home));
        switchMode(Mode::Insert, ic);
        resetState();
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_A)) {
        ic->forwardKey(fcitx::Key(FcitxKey_End));
        switchMode(Mode::Insert, ic);
        resetState();
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_o)) {
        ic->forwardKey(fcitx::Key(FcitxKey_End));
        ic->forwardKey(fcitx::Key(FcitxKey_Return));
        switchMode(Mode::Insert, ic);
        resetState();
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_O)) {
        ic->forwardKey(fcitx::Key(FcitxKey_Home));
        ic->forwardKey(fcitx::Key(FcitxKey_Return));
        ic->forwardKey(fcitx::Key(FcitxKey_Up));
        switchMode(Mode::Insert, ic);
        resetState();
        event.filterAndAccept();
        return;
    }

    // Einfache Befehle
    if (key.check(FcitxKey_x)) {
        for (int i = 0; i < effectiveCount; ++i) {
            ic->forwardKey(fcitx::Key(FcitxKey_Delete));
        }
        resetState();
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_X)) {
        for (int i = 0; i < effectiveCount; ++i) {
            ic->forwardKey(fcitx::Key(FcitxKey_BackSpace));
        }
        resetState();
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_p)) {
        // Zeile darunter einfuegen: End -> Enter -> Paste
        ic->forwardKey(fcitx::Key(FcitxKey_End));
        ic->forwardKey(fcitx::Key(FcitxKey_Return));
        ic->forwardKey(pasteKey(ic));
        resetState();
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_P)) {
        // Zeile darueber einfuegen: Home -> Enter -> Up -> Paste
        ic->forwardKey(fcitx::Key(FcitxKey_Home));
        ic->forwardKey(fcitx::Key(FcitxKey_Return));
        ic->forwardKey(fcitx::Key(FcitxKey_Up));
        ic->forwardKey(pasteKey(ic));
        resetState();
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_u)) {
        for (int i = 0; i < effectiveCount; ++i) {
            ic->forwardKey(fcitx::Key(FcitxKey_z, fcitx::KeyState::Ctrl));
        }
        resetState();
        event.filterAndAccept();
        return;
    }

    // Operatoren -> Operator-Pending
    if (key.check(FcitxKey_d)) {
        pendingOp_ = Operator::Delete;
        switchMode(Mode::OperatorPending, ic);
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_y)) {
        pendingOp_ = Operator::Yank;
        switchMode(Mode::OperatorPending, ic);
        event.filterAndAccept();
        return;
    }
    if (key.check(FcitxKey_c)) {
        pendingOp_ = Operator::Change;
        switchMode(Mode::OperatorPending, ic);
        event.filterAndAccept();
        return;
    }

    // Escape: State zuruecksetzen
    if (key.check(FcitxKey_Escape)) {
        resetState();
        updateModeDisplay(ic);
        event.filterAndAccept();
        return;
    }

    // Druckbare Zeichen konsumieren (Vim Normal: kein Tippen)
    // Alles andere (F-Tasten, Pfeiltasten, Backspace, Enter, Tab, ...) durchlassen
    if (key.sym() >= 0x20 && key.sym() <= 0x7E) {
        event.filterAndAccept();
    }
}

void VimotionEngine::handleOperatorPending(fcitx::KeyEvent &event) {
    auto key = event.key();
    auto *ic = event.inputContext();

    // Escape: zurueck zu Normal
    if (key.check(FcitxKey_Escape)) {
        switchMode(Mode::Normal, ic);
        resetState();
        event.filterAndAccept();
        return;
    }

    int effectiveCount = countActive_ ? count_ : 1;

    // Count im Operator-Pending: Ziffern sammeln
    if (key.sym() >= FcitxKey_1 && key.sym() <= FcitxKey_9) {
        int digit = key.sym() - FcitxKey_0;
        if (countActive_) {
            count_ = count_ * 10 + digit;
        } else {
            count_ = digit;
            countActive_ = true;
        }
        updateModeDisplay(ic);
        event.filterAndAccept();
        return;
    }

    // Gleicher Operator nochmal -> Zeilenoperator (dd/yy/cc)
    if ((pendingOp_ == Operator::Delete && key.check(FcitxKey_d)) ||
        (pendingOp_ == Operator::Yank && key.check(FcitxKey_y)) ||
        (pendingOp_ == Operator::Change && key.check(FcitxKey_c))) {
        executeLineOperator(ic, pendingOp_, effectiveCount);
        switchMode(Mode::Normal, ic);
        resetState();
        event.filterAndAccept();
        return;
    }

    // Pending-g Sequenz: dgg, ygg, cgg
    if (pendingG_) {
        pendingG_ = false;
        if (key.check(FcitxKey_g)) {
            executeOperator(ic, pendingOp_,
                            fcitx::Key(FcitxKey_Home,
                                       fcitx::KeyState::Ctrl_Shift),
                            1);
            if (pendingOp_ != Operator::Change) {
                switchMode(Mode::Normal, ic);
            }
            resetState();
            event.filterAndAccept();
            return;
        }
        switchMode(Mode::Normal, ic);
        resetState();
        event.filterAndAccept();
        return;
    }

    if (key.check(FcitxKey_g)) {
        pendingG_ = true;
        event.filterAndAccept();
        return;
    }

    // e-Motion mit Operator
    if (key.check(FcitxKey_e)) {
        executeMotionE(ic, effectiveCount, true);
        switch (pendingOp_) {
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
            switchMode(Mode::Insert, ic);
            break;
        case Operator::None:
            break;
        }
        if (pendingOp_ != Operator::Change) {
            switchMode(Mode::Normal, ic);
        }
        resetState();
        event.filterAndAccept();
        return;
    }

    // Motion-Lookup: Operator + Motion ausfuehren
    for (const auto &m : getMotionMappings()) {
        if (key.check(m.vimKey)) {
            if (m.vimKey == FcitxKey_0 && countActive_) {
                count_ = count_ * 10;
                event.filterAndAccept();
                return;
            }
            executeOperator(ic, pendingOp_, m.shiftVariant, effectiveCount);
            if (pendingOp_ != Operator::Change) {
                switchMode(Mode::Normal, ic);
            }
            resetState();
            event.filterAndAccept();
            return;
        }
    }

    // Unbekannte Taste: abbrechen
    switchMode(Mode::Normal, ic);
    resetState();
    event.filterAndAccept();
}

void VimotionEngine::executeMotion(fcitx::InputContext *ic, fcitx::Key motion,
                                   int count) {
    for (int i = 0; i < count; ++i) {
        ic->forwardKey(motion);
    }
}

void VimotionEngine::executeMotionE(fcitx::InputContext *ic, int count,
                                    bool withShift) {
    // Wortende: Ctrl+Right (naechster Wortanfang), dann Left (ein zurueck)
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

void VimotionEngine::executeOperator(fcitx::InputContext *ic, Operator op,
                                     fcitx::Key shiftMotion, int count) {
    // Shift+Motion zum Selektieren
    for (int i = 0; i < count; ++i) {
        ic->forwardKey(shiftMotion);
    }

    switch (op) {
    case Operator::Delete:
        ic->forwardKey(fcitx::Key(FcitxKey_Delete));
        break;
    case Operator::Yank:
        ic->forwardKey(copyKey(ic));
        // Selektion aufheben: Right dann Left
        ic->forwardKey(fcitx::Key(FcitxKey_Right));
        ic->forwardKey(fcitx::Key(FcitxKey_Left));
        break;
    case Operator::Change:
        ic->forwardKey(fcitx::Key(FcitxKey_Delete));
        switchMode(Mode::Insert, ic);
        break;
    case Operator::None:
        break;
    }
}

void VimotionEngine::executeLineOperator(fcitx::InputContext *ic, Operator op,
                                         int count) {
    // Home -> Shift+Down (count mal) -> Aktion
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
        // Left kollabiert die Selektion zum Anfang (= original Zeilenstart)
        ic->forwardKey(fcitx::Key(FcitxKey_Left));
        break;
    case Operator::Change:
        // cc: Home -> Shift+End -> Delete -> Insert
        ic->forwardKey(fcitx::Key(FcitxKey_Home));
        ic->forwardKey(fcitx::Key(FcitxKey_End, fcitx::KeyState::Shift));
        ic->forwardKey(fcitx::Key(FcitxKey_Delete));
        switchMode(Mode::Insert, ic);
        break;
    case Operator::None:
        break;
    }
}

void VimotionEngine::switchMode(Mode newMode, fcitx::InputContext *ic) {
    mode_ = newMode;
    updateModeDisplay(ic);
}

void VimotionEngine::updateModeDisplay(fcitx::InputContext *ic) {
    auto &inputPanel = ic->inputPanel();
    inputPanel.reset();

    fcitx::Text aux;
    switch (mode_) {
    case Mode::Normal: {
        std::string display = "[N]";
        if (countActive_) {
            display += " " + std::to_string(count_);
        }
        aux.append(display);
        break;
    }
    case Mode::OperatorPending: {
        std::string display = "[N] ";
        if (countActive_) {
            display += std::to_string(count_);
        }
        switch (pendingOp_) {
        case Operator::Delete: display += "d"; break;
        case Operator::Yank:   display += "y"; break;
        case Operator::Change: display += "c"; break;
        case Operator::None: break;
        }
        if (pendingG_) {
            display += "g";
        }
        aux.append(display);
        break;
    }
    case Mode::Insert:
        aux.append("[I]");
        break;
    }

    inputPanel.setAuxUp(aux);
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

bool VimotionEngine::isTerminal(fcitx::InputContext *ic) const {
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

fcitx::Key VimotionEngine::copyKey(fcitx::InputContext *ic) const {
    if (isTerminal(ic)) {
        return fcitx::Key(FcitxKey_c, fcitx::KeyState::Ctrl_Shift);
    }
    return fcitx::Key(FcitxKey_c, fcitx::KeyState::Ctrl);
}

fcitx::Key VimotionEngine::pasteKey(fcitx::InputContext *ic) const {
    if (isTerminal(ic)) {
        return fcitx::Key(FcitxKey_v, fcitx::KeyState::Ctrl_Shift);
    }
    return fcitx::Key(FcitxKey_v, fcitx::KeyState::Ctrl);
}

void VimotionEngine::forwardKeySequence(
    fcitx::InputContext *ic, const std::vector<fcitx::Key> &keys) {
    for (const auto &key : keys) {
        ic->forwardKey(key);
    }
}

void VimotionEngine::resetState() {
    pendingOp_ = Operator::None;
    count_ = 0;
    countActive_ = false;
    pendingG_ = false;
}

fcitx::AddonInstance *VimotionFactory::create(fcitx::AddonManager *manager) {
    return new VimotionEngine(manager->instance());
}

} // namespace vimotion

FCITX_ADDON_FACTORY(vimotion::VimotionFactory);
