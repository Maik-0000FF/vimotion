#include "motions.h"
#include <fcitx-utils/keysymgen.h>

namespace vimotion {

const std::vector<MotionMapping> &getMotionMappings() {
    static const std::vector<MotionMapping> mappings = {
        // Basis-Motions
        {FcitxKey_h, fcitx::Key(FcitxKey_Left),
                     fcitx::Key(FcitxKey_Left, fcitx::KeyState::Shift)},
        {FcitxKey_l, fcitx::Key(FcitxKey_Right),
                     fcitx::Key(FcitxKey_Right, fcitx::KeyState::Shift)},
        {FcitxKey_j, fcitx::Key(FcitxKey_Down),
                     fcitx::Key(FcitxKey_Down, fcitx::KeyState::Shift)},
        {FcitxKey_k, fcitx::Key(FcitxKey_Up),
                     fcitx::Key(FcitxKey_Up, fcitx::KeyState::Shift)},

        // Wort-Navigation (e wird separat behandelt)
        {FcitxKey_w, fcitx::Key(FcitxKey_Right, fcitx::KeyState::Ctrl),
                     fcitx::Key(FcitxKey_Right, fcitx::KeyState::Ctrl_Shift)},
        {FcitxKey_b, fcitx::Key(FcitxKey_Left, fcitx::KeyState::Ctrl),
                     fcitx::Key(FcitxKey_Left, fcitx::KeyState::Ctrl_Shift)},

        // Zeilen-Navigation
        {FcitxKey_0, fcitx::Key(FcitxKey_Home),
                     fcitx::Key(FcitxKey_Home, fcitx::KeyState::Shift)},
        {FcitxKey_dollar, fcitx::Key(FcitxKey_End),
                          fcitx::Key(FcitxKey_End, fcitx::KeyState::Shift)},

        // Dokument-Navigation (gg wird separat behandelt)
        {FcitxKey_G, fcitx::Key(FcitxKey_End, fcitx::KeyState::Ctrl),
                     fcitx::Key(FcitxKey_End, fcitx::KeyState::Ctrl_Shift)},
    };
    return mappings;
}

} // namespace vimotion
