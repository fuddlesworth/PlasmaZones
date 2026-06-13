// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "generalpage.h"

namespace PhosphorControlExamplesMinimal {

GeneralPage::GeneralPage(QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("general"), parent)
{
}

bool GeneralPage::soundsEnabled() const
{
    return m_stagedSounds;
}

void GeneralPage::setSoundsEnabled(bool v)
{
    if (m_stagedSounds == v) {
        return;
    }
    m_stagedSounds = v;
    Q_EMIT soundsEnabledChanged();
    recomputeDirty();
}

QString GeneralPage::greeting() const
{
    return m_stagedGreeting;
}

void GeneralPage::setGreeting(const QString& g)
{
    if (m_stagedGreeting == g) {
        return;
    }
    m_stagedGreeting = g;
    Q_EMIT greetingChanged();
    recomputeDirty();
}

bool GeneralPage::isDirty() const
{
    return m_dirty;
}

void GeneralPage::apply()
{
    m_persistedSounds = m_stagedSounds;
    m_persistedGreeting = m_stagedGreeting;
    recomputeDirty();
    // ApplicationController::applyAllAsync waits for applyResult to
    // decrement its pending counter — synchronous domains MUST emit it
    // or the chrome's Save button stays "Saving…" until the 60 s
    // batch timeout fires.
    Q_EMIT applyResult(true, QString());
}

void GeneralPage::discard()
{
    if (m_stagedSounds != m_persistedSounds) {
        m_stagedSounds = m_persistedSounds;
        Q_EMIT soundsEnabledChanged();
    }
    if (m_stagedGreeting != m_persistedGreeting) {
        m_stagedGreeting = m_persistedGreeting;
        Q_EMIT greetingChanged();
    }
    recomputeDirty();
    // Symmetric with apply() — discardAllAsync needs discardResult.
    Q_EMIT discardResult(true, QString());
}

void GeneralPage::resetToDefaults()
{
    if (!m_stagedSounds) {
        m_stagedSounds = true;
        Q_EMIT soundsEnabledChanged();
    }
    const QString defaultGreeting = QStringLiteral("Hello, world");
    if (m_stagedGreeting != defaultGreeting) {
        m_stagedGreeting = defaultGreeting;
        Q_EMIT greetingChanged();
    }
    recomputeDirty();
}

void GeneralPage::recomputeDirty()
{
    const bool d = m_stagedSounds != m_persistedSounds || m_stagedGreeting != m_persistedGreeting;
    if (d == m_dirty) {
        return;
    }
    m_dirty = d;
    Q_EMIT dirtyChanged();
}

} // namespace PhosphorControlExamplesMinimal
