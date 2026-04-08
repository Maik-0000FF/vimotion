#ifndef VIMOTION_H
#define VIMOTION_H

#include <string>
#include <vector>
#include <fcitx-utils/key.h>
#include <fcitx/addonfactory.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

namespace vimotion {

enum class Mode { Normal, Insert, OperatorPending };
enum class Operator { None, Delete, Yank, Change };

class VimotionEngine : public fcitx::InputMethodEngineV2 {
public:
    explicit VimotionEngine(fcitx::Instance *instance);

    void keyEvent(const fcitx::InputMethodEntry &entry,
                  fcitx::KeyEvent &event) override;
    void activate(const fcitx::InputMethodEntry &entry,
                  fcitx::InputContextEvent &event) override;
    void deactivate(const fcitx::InputMethodEntry &entry,
                    fcitx::InputContextEvent &event) override;
    void reset(const fcitx::InputMethodEntry &entry,
               fcitx::InputContextEvent &event) override;

private:
    fcitx::Instance *instance_;
    Mode mode_ = Mode::Normal;
    Operator pendingOp_ = Operator::None;
    int count_ = 0;
    bool countActive_ = false;
    bool pendingG_ = false;

    void handleNormalMode(fcitx::KeyEvent &event);
    void handleInsertMode(fcitx::KeyEvent &event);
    void handleOperatorPending(fcitx::KeyEvent &event);

    void executeMotion(fcitx::InputContext *ic, fcitx::Key motion, int count);
    void executeMotionE(fcitx::InputContext *ic, int count, bool withShift);
    void executeOperator(fcitx::InputContext *ic, Operator op,
                         fcitx::Key motion, int count);
    void executeLineOperator(fcitx::InputContext *ic, Operator op, int count);

    void switchMode(Mode newMode, fcitx::InputContext *ic);
    void updateModeDisplay(fcitx::InputContext *ic);
    void forwardKeySequence(fcitx::InputContext *ic,
                            const std::vector<fcitx::Key> &keys);
    bool isTerminal(fcitx::InputContext *ic) const;
    fcitx::Key copyKey(fcitx::InputContext *ic) const;
    fcitx::Key pasteKey(fcitx::InputContext *ic) const;
    void resetState();
};

class VimotionFactory : public fcitx::AddonFactory {
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};

} // namespace vimotion

#endif
