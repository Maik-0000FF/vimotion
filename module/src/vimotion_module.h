#ifndef VIMOTION_MODULE_H
#define VIMOTION_MODULE_H

#include <memory>
#include <string>
#include <vector>
#include <fcitx-utils/key.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/instance.h>

namespace vimotion_module {

enum class Mode { Normal, Insert, OperatorPending };
enum class Operator { None, Delete, Yank, Change };

// Per-InputContext State (jedes Fenster hat eigenen Modus)
struct VimState : public fcitx::InputContextProperty {
    bool enabled = false;
    Mode mode = Mode::Normal;
    Operator pendingOp = Operator::None;
    int count = 0;
    bool countActive = false;
    bool pendingG = false;
};

class VimotionModule : public fcitx::AddonInstance {
public:
    explicit VimotionModule(fcitx::Instance *instance);

private:
    fcitx::Instance *instance_;
    fcitx::FactoryFor<VimState> factory_{
        [](fcitx::InputContext &) { return new VimState; }};
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> keyHandler_;

    // Blacklist: Apps in denen vimotion deaktiviert ist
    std::vector<std::string> blacklist_ = {
        "nvim", "vim", "neovim"
    };

    // Toggle-Hotkey: Ctrl+Escape
    fcitx::Key toggleKey_{FcitxKey_Escape, fcitx::KeyState::Ctrl};

    void handleKeyEvent(fcitx::Event &event);
    void handleNormalMode(VimState *state, fcitx::KeyEvent &event);
    void handleInsertMode(VimState *state, fcitx::KeyEvent &event);
    void handleOperatorPending(VimState *state, fcitx::KeyEvent &event);

    void executeMotion(fcitx::InputContext *ic, fcitx::Key motion, int count);
    void executeMotionE(fcitx::InputContext *ic, int count, bool withShift);
    void executeOperator(fcitx::InputContext *ic, VimState *state,
                         Operator op, fcitx::Key shiftMotion, int count);
    void executeLineOperator(fcitx::InputContext *ic, VimState *state,
                             Operator op, int count);

    void switchMode(VimState *state, Mode newMode, fcitx::InputContext *ic);
    void updateModeDisplay(VimState *state, fcitx::InputContext *ic);
    void resetState(VimState *state);

    bool isBlacklisted(fcitx::InputContext *ic) const;
    bool isTerminal(fcitx::InputContext *ic) const;
    fcitx::Key copyKey(fcitx::InputContext *ic) const;
    fcitx::Key pasteKey(fcitx::InputContext *ic) const;
};

class VimotionModuleFactory : public fcitx::AddonFactory {
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};

} // namespace vimotion_module

#endif
