// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorTheme/phosphortheme_export.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace PhosphorTheme {

/**
 * @brief The shell's two type faces, resolved against what is installed.
 *
 * The identity (05 §7) sets Manrope for display and UI text and JetBrains
 * Mono for every value. Neither ships with a desktop, and binding the name
 * unconditionally hands the choice to fontconfig, which substitutes by its
 * own rules (often a serif, or the wrong weight axis). This singleton asks
 * the font database instead and walks a short preference list per role,
 * ending on the system face, so `Tokens.font_family_ui` always names a
 * family that exists.
 *
 * Resolved once on construction (the QML engine constructs the singleton
 * on first access, after the platform font database exists) and again on
 * `refresh()`.
 */
class PHOSPHORTHEME_EXPORT FontFaces : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The display and UI family.
    Q_PROPERTY(QString ui READ ui NOTIFY facesChanged)
    /// The tabular value family.
    Q_PROPERTY(QString mono READ mono NOTIFY facesChanged)
    /// Whether the preferred faces (Manrope, JetBrains Mono) were found.
    Q_PROPERTY(bool uiPreferred READ uiPreferred NOTIFY facesChanged)
    Q_PROPERTY(bool monoPreferred READ monoPreferred NOTIFY facesChanged)

public:
    explicit FontFaces(QObject* parent = nullptr);

    [[nodiscard]] QString ui() const;
    [[nodiscard]] QString mono() const;
    [[nodiscard]] bool uiPreferred() const;
    [[nodiscard]] bool monoPreferred() const;

    /// The preference lists, first entry the identity's face.
    [[nodiscard]] static QStringList uiCandidates();
    [[nodiscard]] static QStringList monoCandidates();

    /// First installed family from @p candidates, else @p fallback.
    [[nodiscard]] static QString resolve(const QStringList& candidates, const QString& fallback);

    /// Re-read the font database (a font installed while the shell runs).
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void facesChanged();

private:
    QString m_ui;
    QString m_mono;
    bool m_uiPreferred = false;
    bool m_monoPreferred = false;
};

} // namespace PhosphorTheme
