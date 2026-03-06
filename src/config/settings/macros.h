// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Simple setter: if changed, update member, emit specific signal, emit settingsChanged
#define SETTINGS_SETTER(Type, name, member, signal)                                                                    \
    void Settings::set##name(Type value)                                                                               \
    {                                                                                                                  \
        if (member != value) {                                                                                         \
            member = value;                                                                                            \
            Q_EMIT signal();                                                                                           \
            Q_EMIT settingsChanged();                                                                                  \
        }                                                                                                              \
    }

// Clamped int setter: clamp value, then apply if changed
#define SETTINGS_SETTER_CLAMPED(name, member, signal, minVal, maxVal)                                                  \
    void Settings::set##name(int value)                                                                                \
    {                                                                                                                  \
        value = qBound(minVal, value, maxVal);                                                                         \
        if (member != value) {                                                                                         \
            member = value;                                                                                            \
            Q_EMIT signal();                                                                                           \
            Q_EMIT settingsChanged();                                                                                  \
        }                                                                                                              \
    }

// Clamped qreal setter: clamp + qFuzzyCompare (safe near zero via 1.0+x trick)
#define SETTINGS_SETTER_CLAMPED_QREAL(name, member, signal, minVal, maxVal)                                            \
    void Settings::set##name(qreal value)                                                                              \
    {                                                                                                                  \
        value = qBound(qreal(minVal), value, qreal(maxVal));                                                           \
        if (!qFuzzyCompare(1.0 + member, 1.0 + value)) {                                                               \
            member = value;                                                                                            \
            Q_EMIT signal();                                                                                           \
            Q_EMIT settingsChanged();                                                                                  \
        }                                                                                                              \
    }

// Int-to-enum adapter: validate range, then delegate to typed setter
#define SETTINGS_SETTER_ENUM_INT(name, EnumType, minVal, maxVal)                                                       \
    void Settings::set##name##Int(int value)                                                                           \
    {                                                                                                                  \
        if (value >= (minVal) && value <= (maxVal)) {                                                                  \
            set##name(static_cast<EnumType>(value));                                                                   \
        }                                                                                                              \
    }
