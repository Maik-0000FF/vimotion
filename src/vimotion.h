#ifndef VIMOTION_H
#define VIMOTION_H

#include <memory>
#include <string>
#include <vector>
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysymgen.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/instance.h>

namespace vimotion {

FCITX_CONFIG_ENUM(AppFilterMode, None, Blacklist, Whitelist);

FCITX_CONFIGURATION(
    GeneralConfig,
    fcitx::Option<bool> enabledByDefault{this, "EnabledByDefault",
        "Auto-enable on startup", false};
    fcitx::Option<fcitx::KeyList> toggleKey{this, "ToggleKey", "Toggle Key",
        {fcitx::Key(FcitxKey_Escape, fcitx::KeyState::Ctrl)}};
);

FCITX_CONFIGURATION(
    AppFilterConfig,
    fcitx::Option<AppFilterMode> mode{this, "Mode", "Filter Mode",
        AppFilterMode::Blacklist};
    fcitx::Option<std::vector<std::string>> blacklist{
        this, "Blacklist",
        "Blacklist (apps where vimotion is disabled)",
        {"nvim", "vim", "neovim"}};
    fcitx::Option<std::vector<std::string>> whitelist{
        this, "Whitelist",
        "Whitelist (apps where vimotion is enabled)",
        {}};
);

FCITX_CONFIGURATION(
    MappingsConfig,
    fcitx::Option<int, fcitx::IntConstrain> timeoutMs{
        this, "TimeoutMs",
        "Sequence timeout in milliseconds",
        200, fcitx::IntConstrain(50, 2000)};
    // Format: "<sequence>=<keysym>" e.g. "jk=Escape"
    fcitx::Option<std::vector<std::string>> insertMap{
        this, "InsertMap",
        "Insert Mode Mappings (format: 'jk=Escape')",
        {std::string("jk=Escape")}};
);

FCITX_CONFIGURATION(
    VimotionConfig,
    fcitx::Option<GeneralConfig> general{this, "General", "General"};
    fcitx::Option<AppFilterConfig> appFilter{this, "AppFilter", "App Filter"};
    fcitx::Option<MappingsConfig> mappings{this, "Mappings", "Key Mappings"};
);

enum class Mode { Normal, Insert, OperatorPending };
enum class Operator { None, Delete, Yank, Change };

struct InsertMapping {
    std::string from;       // sequence text, e.g. "jk"
    fcitx::Key target;      // mapped target key, e.g. Escape
};

// Per-InputContext State (jedes Fenster hat eigenen Modus)
struct VimState : public fcitx::InputContextProperty {
    bool enabled = false;
    Mode mode = Mode::Normal;
    Operator pendingOp = Operator::None;
    int count = 0;
    bool countActive = false;
    bool pendingG = false;

    // Insert-mode sequence buffer (for jk -> Escape style mappings)
    std::string seqText;
    std::vector<fcitx::Key> seqKeys;
    std::unique_ptr<fcitx::EventSourceTime> seqTimer;
};

class Vimotion : public fcitx::AddonInstance {
public:
    explicit Vimotion(fcitx::Instance *instance);

    const fcitx::Configuration *getConfig() const override { return &config_; }
    void setConfig(const fcitx::RawConfig &rawConfig) override;
    void reloadConfig() override;

private:
    void loadConfigFromFile();
    void applyConfig();
    void parseInsertMappings();

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

    bool isFiltered(fcitx::InputContext *ic) const;
    bool isTerminal(fcitx::InputContext *ic) const;
    fcitx::Key copyKey(fcitx::InputContext *ic) const;
    fcitx::Key pasteKey(fcitx::InputContext *ic) const;

    // Insert-mode sequence handling
    bool isPrintableAscii(fcitx::KeySym sym) const;
    void flushSeqBuffer(VimState *state, fcitx::InputContext *ic);
    void clearSeqBuffer(VimState *state);
    void scheduleSeqTimeout(VimState *state, fcitx::InputContext *ic);

    fcitx::Instance *instance_;
    fcitx::FactoryFor<VimState> factory_{
        [](fcitx::InputContext &) { return new VimState; }};
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> keyHandler_;
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>
        focusInHandler_;
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>
        focusOutHandler_;

    VimotionConfig config_;

    // Cached values from config_
    bool enabledByDefault_ = false;
    fcitx::KeyList toggleKeys_{
        {fcitx::Key(FcitxKey_Escape, fcitx::KeyState::Ctrl)}};
    AppFilterMode filterMode_ = AppFilterMode::Blacklist;
    std::vector<std::string> blacklist_{"nvim", "vim", "neovim"};
    std::vector<std::string> whitelist_;
    int seqTimeoutMs_ = 200;
    std::vector<InsertMapping> insertMappings_;
};

class VimotionFactory : public fcitx::AddonFactory {
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};

} // namespace vimotion

#endif
