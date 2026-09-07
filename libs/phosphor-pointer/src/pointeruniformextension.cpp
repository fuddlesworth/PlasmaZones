// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPointer/PointerUniformExtension.h>

#include <PhosphorPointer/PointerShaderContract.h>

#include <QMutexLocker>

#include <cstring>

namespace PhosphorPointerShaders {

PointerUniformExtension::PointerUniformExtension()
{
    std::memset(&m_data, 0, sizeof(m_data));
    // No press or release yet: the contract's "none this session" sentinel.
    m_data.uPointerPress[2] = 1.0e6f;
    m_data.uPointerRelease[2] = 1.0e6f;
    // No motion yet, unit scale.
    m_data.uPointerState[1] = 1.0e6f;
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

void PointerUniformExtension::setVelocity(const QVector2D& velocity)
{
    QMutexLocker lock(&m_mutex);
    setVec4Locked(m_data.uPointerVelocity, velocity.x(), velocity.y(), velocity.length(), 0.0f);
}

void PointerUniformExtension::setPress(const QPointF& pos, double secondsSince, int button)
{
    QMutexLocker lock(&m_mutex);
    setVec4Locked(m_data.uPointerPress, static_cast<float>(pos.x()), static_cast<float>(pos.y()),
                  static_cast<float>(secondsSince), static_cast<float>(button));
}

void PointerUniformExtension::setRelease(const QPointF& pos, double secondsSince, int button)
{
    QMutexLocker lock(&m_mutex);
    setVec4Locked(m_data.uPointerRelease, static_cast<float>(pos.x()), static_cast<float>(pos.y()),
                  static_cast<float>(secondsSince), static_cast<float>(button));
}

void PointerUniformExtension::setState(int buttonsMask, double idleSeconds, double scale)
{
    QMutexLocker lock(&m_mutex);
    // .w is the trail count, owned by setTrail; keep it.
    setVec4Locked(m_data.uPointerState, static_cast<float>(buttonsMask), static_cast<float>(idleSeconds),
                  static_cast<float>(scale), m_data.uPointerState[3]);
}

void PointerUniformExtension::setCursorRect(const QRectF& rect)
{
    QMutexLocker lock(&m_mutex);
    setVec4Locked(m_data.uCursorRect, static_cast<float>(rect.x()), static_cast<float>(rect.y()),
                  static_cast<float>(rect.width()), static_cast<float>(rect.height()));
}

void PointerUniformExtension::setHasCursorSprite(bool has)
{
    QMutexLocker lock(&m_mutex);
    setVec4Locked(m_data.uPointerFlags, has ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
}

void PointerUniformExtension::setTrail(const QList<QVector4D>& trail)
{
    QMutexLocker lock(&m_mutex);
    const int count = static_cast<int>(qMin<qsizetype>(trail.size(), PointerShaderContract::kMaxTrailPoints));
    bool changed = false;
    for (int i = 0; i < PointerShaderContract::kMaxTrailPoints; ++i) {
        const QVector4D v = i < count ? trail[i] : QVector4D();
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

void PointerUniformExtension::apply(const PointerFrameState& state)
{
    setVelocity(state.velocity);
    setPress(state.pressPos, state.pressSecondsSince, state.pressButton);
    setRelease(state.releasePos, state.releaseSecondsSince, state.releaseButton);
    setState(state.buttons, state.idleSeconds, state.scale);
    setCursorRect(state.cursorRect);
    setHasCursorSprite(state.hasSprite);
    setTrail(state.trail);
}

} // namespace PhosphorPointerShaders
