#ifndef VIMOTION_MOTIONS_H
#define VIMOTION_MOTIONS_H

#include <vector>
#include <fcitx-utils/key.h>

namespace vimotion_module {

struct MotionMapping {
    fcitx::KeySym vimKey;
    fcitx::Key forwardKey;
    fcitx::Key shiftVariant; // fuer Operator+Motion (Selektion)
};

const std::vector<MotionMapping> &getMotionMappings();

} // namespace vimotion_module

#endif
