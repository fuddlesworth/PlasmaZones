// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPointer/PointerUniformExtension.h>

#include <PhosphorPointer/PointerShaderContract.h>

#include <QMutexLocker>

#include <algorithm>
#include <cstring>

namespace PhosphorPointerShaders {

PointerUniformExtension::PointerUniformExtension()
{
    std::memset(&m_data, 0, sizeof(m_data));
    // No press or release yet: the contract's "none this session" sentinel.
    constexpr float never = static_cast<float>(PointerShaderContract::kNeverSeconds);
    m_data.uPointerPress[2] = never;
    m_data.uPointerRelease[2] = never;
    // No motion yet, unit scale.
    m_data.uPointerState[1] = never;
    m_data.uPointerState[2] = 1.0f;
}

int PointerUniformExtension::extensionSize() const
{
    return static_cast<int>(sizeof(m_data));
}

void PointerUniformExtension::write(char* buffer, int offset) const
{
    QMutexLocker lock(&m_mutex);
    std::memcpy(buffer + offset, &m_data, sizeof(m_data));
}

bool PointerUniformExtension::isDirty() const
{
    return m_dirty.load(std::memory_order_acquire);
}

void PointerUniformExtension::clearDirty()
{
    m_dirty.store(false, std::memory_order_release);
}

void PointerUniformExtension::setVec4Locked(float (&dst)[4], float x, float y, float z, float w)
{
    if (dst[0] == x && dst[1] == y && dst[2] == z && dst[3] == w) {
        return;
    }
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
    dst[3] = w;
    m_dirty.store(true, std::memory_order_release);
}

void PointerUniformExtension::setVelocityLocked(const QVector2D& velocity)
{
    setVec4Locked(m_data.uPointerVelocity, velocity.x(), velocity.y(), velocity.length(), 0.0f);
}

void PointerUniformExtension::setPressLocked(const QPointF& pos, double secondsSince, int button)
{
    setVec4Locked(m_data.uPointerPress, static_cast<float>(pos.x()), static_cast<float>(pos.y()),
                  static_cast<float>(secondsSince), static_cast<float>(button));
}

void PointerUniformExtension::setReleaseLocked(const QPointF& pos, double secondsSince, int button)
{
    setVec4Locked(m_data.uPointerRelease, static_cast<float>(pos.x()), static_cast<float>(pos.y()),
                  static_cast<float>(secondsSince), static_cast<float>(button));
}

void PointerUniformExtension::setStateLocked(int buttonsMask, double idleSeconds, double scale)
{
    // .w is the trail count, owned by setTrail; keep it.
    setVec4Locked(m_data.uPointerState, static_cast<float>(buttonsMask), static_cast<float>(idleSeconds),
                  static_cast<float>(scale), m_data.uPointerState[3]);
}

void PointerUniformExtension::setCursorRectLocked(const QRectF& rect)
{
    setVec4Locked(m_data.uCursorRect, static_cast<float>(rect.x()), static_cast<float>(rect.y()),
                  static_cast<float>(rect.width()), static_cast<float>(rect.height()));
}

void PointerUniformExtension::setFlagsLocked(bool hasSprite, double scale)
{
    // Both flag lanes in one write: the sprite flag from the frame, the reach
    // from the pack, scaled by the frame's canvas scale.
    const float s = scale > 0.0 ? static_cast<float>(scale) : 1.0f;
    setVec4Locked(m_data.uPointerFlags, hasSprite ? 1.0f : 0.0f, static_cast<float>(m_reachLogicalPx) * s, 0.0f, 0.0f);
}

void PointerUniformExtension::setTrailLocked(std::span<const QVector4D> trail)
{
    const int count = static_cast<int>(std::min<size_t>(trail.size(), PointerShaderContract::kMaxTrailPoints));
    bool changed = false;
    for (int i = 0; i < PointerShaderContract::kMaxTrailPoints; ++i) {
        const QVector4D v = i < count ? trail[static_cast<size_t>(i)] : QVector4D();
        float* dst = m_data.uPointerTrail[i];
        if (dst[0] != v.x() || dst[1] != v.y() || dst[2] != v.z() || dst[3] != v.w()) {
            dst[0] = v.x();
            dst[1] = v.y();
            dst[2] = v.z();
            dst[3] = v.w();
            changed = true;
        }
    }
    const float countF = static_cast<float>(count);
    if (m_data.uPointerState[3] != countF) {
        m_data.uPointerState[3] = countF;
        changed = true;
    }
    if (changed) {
        m_dirty.store(true, std::memory_order_release);
    }
}

void PointerUniformExtension::setVelocity(const QVector2D& velocity)
{
    QMutexLocker lock(&m_mutex);
    setVelocityLocked(velocity);
}

void PointerUniformExtension::setPress(const QPointF& pos, double secondsSince, int button)
{
    QMutexLocker lock(&m_mutex);
    setPressLocked(pos, secondsSince, button);
}

void PointerUniformExtension::setRelease(const QPointF& pos, double secondsSince, int button)
{
    QMutexLocker lock(&m_mutex);
    setReleaseLocked(pos, secondsSince, button);
}

void PointerUniformExtension::setState(int buttonsMask, double idleSeconds, double scale)
{
    QMutexLocker lock(&m_mutex);
    setStateLocked(buttonsMask, idleSeconds, scale);
}

void PointerUniformExtension::setCursorRect(const QRectF& rect)
{
    QMutexLocker lock(&m_mutex);
    setCursorRectLocked(rect);
}

void PointerUniformExtension::setHasCursorSprite(bool has)
{
    QMutexLocker lock(&m_mutex);
    // .y is the reach, owned by setReachLogicalPx / apply — keep it.
    setVec4Locked(m_data.uPointerFlags, has ? 1.0f : 0.0f, m_data.uPointerFlags[1], 0.0f, 0.0f);
}

void PointerUniformExtension::setReachLogicalPx(double reach)
{
    QMutexLocker lock(&m_mutex);
    m_reachLogicalPx = std::max(reach, 0.0);
    // Re-derive the device value against the scale already in the state lane
    // so a reach change lands this frame rather than on the next apply.
    const float scale = m_data.uPointerState[2] > 0.0f ? m_data.uPointerState[2] : 1.0f;
    setVec4Locked(m_data.uPointerFlags, m_data.uPointerFlags[0], static_cast<float>(m_reachLogicalPx) * scale, 0.0f,
                  0.0f);
}

void PointerUniformExtension::setTrail(std::span<const QVector4D> trail)
{
    QMutexLocker lock(&m_mutex);
    setTrailLocked(trail);
}

void PointerUniformExtension::apply(const PointerFrameState& state)
{
    // One lock for the whole frame. write() takes the same mutex, so the
    // render thread can never copy a tail with this frame's velocity and the
    // previous frame's trail: the frame lands atomically or not at all.
    QMutexLocker lock(&m_mutex);
    setVelocityLocked(state.velocity);
    setPressLocked(state.pressPos, state.pressSecondsSince, state.pressButton);
    setReleaseLocked(state.releasePos, state.releaseSecondsSince, state.releaseButton);
    setStateLocked(state.buttons, state.idleSeconds, state.scale);
    setCursorRectLocked(state.cursorRect);
    setFlagsLocked(state.hasSprite, state.scale);
    const int count = std::clamp(state.trailCount, 0, PointerShaderContract::kMaxTrailPoints);
    setTrailLocked(std::span<const QVector4D>(state.trail.data(), static_cast<size_t>(count)));
}

} // namespace PhosphorPointerShaders
