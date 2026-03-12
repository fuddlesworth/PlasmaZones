// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcmeditor.h"
#include <QDBusConnection>
#include <KPluginFactory>
#include <KSharedConfig>
#include "../common/dbusutils.h"
#include "../../src/core/constants.h"

K_PLUGIN_CLASS_WITH_JSON(PlasmaZones::KCMEditor, "kcm_plasmazones_editor.json")

namespace {
constexpr int ShiftMod = static_cast<int>(Qt::ShiftModifier);
constexpr int CtrlMod = static_cast<int>(Qt::ControlModifier);
}

namespace PlasmaZones {

KCMEditor::KCMEditor(QObject* parent, const KPluginMetaData& data)
    : KQuickConfigModule(parent, data)
{
    setButtons(Apply | Default);
}

// ── Config group ─────────────────────────────────────────────────────────

KConfigGroup KCMEditor::editorConfigGroup()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    return config->group(QStringLiteral("Editor"));
}

// ── Load / Save / Defaults ───────────────────────────────────────────────

void KCMEditor::load()
{
    KQuickConfigModule::load();
    // Re-read from disk (in case daemon changed the file)
    KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"))->reparseConfiguration();
    emitAllChanged();
    setNeedsSave(false);
}

void KCMEditor::save()
{
    // Settings are written immediately to KConfig on each setter call,
    // so we just need to notify the daemon to reload.
    KCMDBus::notifyReload();

    KQuickConfigModule::save();
    setNeedsSave(false);
}

void KCMEditor::defaults()
{
    KQuickConfigModule::defaults();
    resetEditorShortcuts();
    setEditorGridSnappingEnabled(true);
    setEditorEdgeSnappingEnabled(true);
    setEditorSnapIntervalX(0.05);
    setEditorSnapIntervalY(0.05);
    setEditorSnapOverrideModifier(defaultEditorSnapOverrideModifier());
    setFillOnDropEnabled(true);
    setFillOnDropModifier(defaultFillOnDropModifier());
}

// ── Shortcut getters ─────────────────────────────────────────────────────

QString KCMEditor::editorDuplicateShortcut() const
{
    return editorConfigGroup().readEntry(QLatin1String("EditorDuplicateShortcut"), QStringLiteral("Ctrl+D"));
}

QString KCMEditor::editorSplitHorizontalShortcut() const
{
    return editorConfigGroup().readEntry(QLatin1String("EditorSplitHorizontalShortcut"),
                                         QStringLiteral("Ctrl+Shift+H"));
}

QString KCMEditor::editorSplitVerticalShortcut() const
{
    return editorConfigGroup().readEntry(QLatin1String("EditorSplitVerticalShortcut"), QStringLiteral("Ctrl+Alt+V"));
}

QString KCMEditor::editorFillShortcut() const
{
    return editorConfigGroup().readEntry(QLatin1String("EditorFillShortcut"), QStringLiteral("Ctrl+Shift+F"));
}

// ── Shortcut setters ─────────────────────────────────────────────────────

void KCMEditor::setEditorDuplicateShortcut(const QString& shortcut)
{
    if (editorDuplicateShortcut() != shortcut) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("EditorDuplicateShortcut"), shortcut);
        group.sync();
        Q_EMIT editorDuplicateShortcutChanged();
        setNeedsSave(true);
    }
}

void KCMEditor::setEditorSplitHorizontalShortcut(const QString& shortcut)
{
    if (editorSplitHorizontalShortcut() != shortcut) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("EditorSplitHorizontalShortcut"), shortcut);
        group.sync();
        Q_EMIT editorSplitHorizontalShortcutChanged();
        setNeedsSave(true);
    }
}

void KCMEditor::setEditorSplitVerticalShortcut(const QString& shortcut)
{
    if (editorSplitVerticalShortcut() != shortcut) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("EditorSplitVerticalShortcut"), shortcut);
        group.sync();
        Q_EMIT editorSplitVerticalShortcutChanged();
        setNeedsSave(true);
    }
}

void KCMEditor::setEditorFillShortcut(const QString& shortcut)
{
    if (editorFillShortcut() != shortcut) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("EditorFillShortcut"), shortcut);
        group.sync();
        Q_EMIT editorFillShortcutChanged();
        setNeedsSave(true);
    }
}

// ── Snapping getters ─────────────────────────────────────────────────────

bool KCMEditor::editorGridSnappingEnabled() const
{
    return editorConfigGroup().readEntry(QLatin1String("GridSnappingEnabled"), true);
}

bool KCMEditor::editorEdgeSnappingEnabled() const
{
    return editorConfigGroup().readEntry(QLatin1String("EdgeSnappingEnabled"), true);
}

qreal KCMEditor::editorSnapIntervalX() const
{
    KConfigGroup group = editorConfigGroup();
    qreal intervalX = group.readEntry(QLatin1String("SnapIntervalX"), -1.0);
    if (intervalX < 0.0) {
        return group.readEntry(QLatin1String("SnapInterval"), 0.05);
    }
    return intervalX;
}

qreal KCMEditor::editorSnapIntervalY() const
{
    KConfigGroup group = editorConfigGroup();
    qreal intervalY = group.readEntry(QLatin1String("SnapIntervalY"), -1.0);
    if (intervalY < 0.0) {
        return group.readEntry(QLatin1String("SnapInterval"), 0.05);
    }
    return intervalY;
}

int KCMEditor::editorSnapOverrideModifier() const
{
    return editorConfigGroup().readEntry(QLatin1String("SnapOverrideModifier"), ShiftMod);
}

// ── Snapping setters ─────────────────────────────────────────────────────

void KCMEditor::setEditorGridSnappingEnabled(bool enabled)
{
    if (editorGridSnappingEnabled() != enabled) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("GridSnappingEnabled"), enabled);
        group.sync();
        Q_EMIT editorGridSnappingEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMEditor::setEditorEdgeSnappingEnabled(bool enabled)
{
    if (editorEdgeSnappingEnabled() != enabled) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("EdgeSnappingEnabled"), enabled);
        group.sync();
        Q_EMIT editorEdgeSnappingEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMEditor::setEditorSnapIntervalX(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(editorSnapIntervalX(), interval)) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("SnapIntervalX"), interval);
        group.sync();
        Q_EMIT editorSnapIntervalXChanged();
        setNeedsSave(true);
    }
}

void KCMEditor::setEditorSnapIntervalY(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(editorSnapIntervalY(), interval)) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("SnapIntervalY"), interval);
        group.sync();
        Q_EMIT editorSnapIntervalYChanged();
        setNeedsSave(true);
    }
}

void KCMEditor::setEditorSnapOverrideModifier(int modifier)
{
    if (editorSnapOverrideModifier() != modifier) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("SnapOverrideModifier"), modifier);
        group.sync();
        Q_EMIT editorSnapOverrideModifierChanged();
        setNeedsSave(true);
    }
}

// ── Fill on drop getters ─────────────────────────────────────────────────

bool KCMEditor::fillOnDropEnabled() const
{
    return editorConfigGroup().readEntry(QLatin1String("FillOnDropEnabled"), true);
}

int KCMEditor::fillOnDropModifier() const
{
    return editorConfigGroup().readEntry(QLatin1String("FillOnDropModifier"), CtrlMod);
}

// ── Fill on drop setters ─────────────────────────────────────────────────

void KCMEditor::setFillOnDropEnabled(bool enabled)
{
    if (fillOnDropEnabled() != enabled) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("FillOnDropEnabled"), enabled);
        group.sync();
        Q_EMIT fillOnDropEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMEditor::setFillOnDropModifier(int modifier)
{
    if (fillOnDropModifier() != modifier) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("FillOnDropModifier"), modifier);
        group.sync();
        Q_EMIT fillOnDropModifierChanged();
        setNeedsSave(true);
    }
}

// ── Default values ───────────────────────────────────────────────────────

QString KCMEditor::defaultEditorDuplicateShortcut() const
{
    return QStringLiteral("Ctrl+D");
}

QString KCMEditor::defaultEditorSplitHorizontalShortcut() const
{
    return QStringLiteral("Ctrl+Shift+H");
}

QString KCMEditor::defaultEditorSplitVerticalShortcut() const
{
    return QStringLiteral("Ctrl+Alt+V");
}

QString KCMEditor::defaultEditorFillShortcut() const
{
    return QStringLiteral("Ctrl+Shift+F");
}

int KCMEditor::defaultEditorSnapOverrideModifier() const
{
    return ShiftMod;
}

int KCMEditor::defaultFillOnDropModifier() const
{
    return CtrlMod;
}

// ── Reset shortcuts ──────────────────────────────────────────────────────

void KCMEditor::resetEditorShortcuts()
{
    KConfigGroup group = editorConfigGroup();
    group.writeEntry(QLatin1String("EditorDuplicateShortcut"), QStringLiteral("Ctrl+D"));
    group.writeEntry(QLatin1String("EditorSplitHorizontalShortcut"), QStringLiteral("Ctrl+Shift+H"));
    group.writeEntry(QLatin1String("EditorSplitVerticalShortcut"), QStringLiteral("Ctrl+Alt+V"));
    group.writeEntry(QLatin1String("EditorFillShortcut"), QStringLiteral("Ctrl+Shift+F"));
    group.sync();
    Q_EMIT editorDuplicateShortcutChanged();
    Q_EMIT editorSplitHorizontalShortcutChanged();
    Q_EMIT editorSplitVerticalShortcutChanged();
    Q_EMIT editorFillShortcutChanged();
    setNeedsSave(true);
}

// ── Helpers ──────────────────────────────────────────────────────────────

void KCMEditor::emitAllChanged()
{
    Q_EMIT editorDuplicateShortcutChanged();
    Q_EMIT editorSplitHorizontalShortcutChanged();
    Q_EMIT editorSplitVerticalShortcutChanged();
    Q_EMIT editorFillShortcutChanged();
    Q_EMIT editorGridSnappingEnabledChanged();
    Q_EMIT editorEdgeSnappingEnabledChanged();
    Q_EMIT editorSnapIntervalXChanged();
    Q_EMIT editorSnapIntervalYChanged();
    Q_EMIT editorSnapOverrideModifierChanged();
    Q_EMIT fillOnDropEnabledChanged();
    Q_EMIT fillOnDropModifierChanged();
}

} // namespace PlasmaZones

#include "kcmeditor.moc"
