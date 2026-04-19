// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// AnimatedValue<T> is a header-only template (see AnimatedValue.h). This
// translation unit exists only to host the library-wide logging category
// so every instantiation shares one `phosphoranimation.animatedvalue`
// rule string, rather than each template instantiation getting its own
// static category.

#include <PhosphorAnimation/AnimatedValue.h>

namespace PhosphorAnimation {

Q_LOGGING_CATEGORY(lcAnimatedValue, "phosphoranimation.animatedvalue")

} // namespace PhosphorAnimation
