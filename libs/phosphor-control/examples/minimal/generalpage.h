// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "PhosphorControl/PageController.h"

namespace PhosphorControlExamplesMinimal {

/** A toy page with two staged values (a bool + a string) to demonstrate
 *  dirty propagation, apply, and discard. */
class GeneralPage : public PhosphorControl::PageController
{
    Q_OBJECT
    Q_PROPERTY(bool soundsEnabled READ soundsEnabled WRITE setSoundsEnabled NOTIFY soundsEnabledChanged)
    Q_PROPERTY(QString greeting READ greeting WRITE setGreeting NOTIFY greetingChanged)
    QML_NAMED_ELEMENT(GeneralPage)
    QML_UNCREATABLE("Instantiated by the demo app in C++.")

public:
    explicit GeneralPage(QObject* parent = nullptr);

    bool soundsEnabled() const;
    void setSoundsEnabled(bool v);

    QString greeting() const;
    void setGreeting(const QString& g);

    bool isDirty() const override;

public Q_SLOTS:
    void apply() override;
    void discard() override;
    void resetToDefaults() override;

Q_SIGNALS:
    void soundsEnabledChanged();
    void greetingChanged();

private:
    void recomputeDirty();

    bool m_persistedSounds = true;
    QString m_persistedGreeting = QStringLiteral("Hello, world");

    bool m_stagedSounds = true;
    QString m_stagedGreeting = QStringLiteral("Hello, world");

    bool m_dirty = false;
};

} // namespace PhosphorControlExamplesMinimal
