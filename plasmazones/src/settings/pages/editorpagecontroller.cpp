// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "editorpagecontroller.h"

#include "config/configdefaults.h"
#include "core/interfaces/isettings.h"

namespace PlasmaZones {

EditorPageController::EditorPageController(ISettings& settings, QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("editor"), parent)
    , m_settings(&settings)
{
    // Forward each Settings NOTIFY to the local Q_PROPERTY NOTIFY + the
    // generic `changed()` signal. The shared Settings instance is the source
    // of truth; the sub-controller is a pure facade. Each forward re-emits
    // two signals per change, so signal-to-signal connect() isn't sufficient.
    const auto forward = [this](auto settingsSignal, auto mySignal) {
        connect(m_settings, settingsSignal, this, [this, mySignal]() {
            Q_EMIT(this->*mySignal)();
            Q_EMIT changed();
        });
    };
    forward(&ISettings::editorDuplicateShortcutChanged, &EditorPageController::duplicateShortcutChanged);
    forward(&ISettings::editorSplitHorizontalShortcutChanged, &EditorPageController::splitHorizontalShortcutChanged);
    forward(&ISettings::editorSplitVerticalShortcutChanged, &EditorPageController::splitVerticalShortcutChanged);
    forward(&ISettings::editorFillShortcutChanged, &EditorPageController::fillShortcutChanged);
    forward(&ISettings::editorGridSnappingEnabledChanged, &EditorPageController::gridSnappingEnabledChanged);
    forward(&ISettings::editorEdgeSnappingEnabledChanged, &EditorPageController::edgeSnappingEnabledChanged);
    forward(&ISettings::editorSnapIntervalXChanged, &EditorPageController::snapIntervalXChanged);
    forward(&ISettings::editorSnapIntervalYChanged, &EditorPageController::snapIntervalYChanged);
    forward(&ISettings::editorSnapOverrideModifierChanged, &EditorPageController::snapOverrideModifierChanged);
    forward(&ISettings::fillOnDropEnabledChanged, &EditorPageController::fillOnDropEnabledChanged);
    forward(&ISettings::fillOnDropModifierChanged, &EditorPageController::fillOnDropModifierChanged);
}

QString EditorPageController::duplicateShortcut() const
{
    return m_settings->editorDuplicateShortcut();
}
QString EditorPageController::splitHorizontalShortcut() const
{
    return m_settings->editorSplitHorizontalShortcut();
}
QString EditorPageController::splitVerticalShortcut() const
{
    return m_settings->editorSplitVerticalShortcut();
}
QString EditorPageController::fillShortcut() const
{
    return m_settings->editorFillShortcut();
}
bool EditorPageController::gridSnappingEnabled() const
{
    return m_settings->editorGridSnappingEnabled();
}
bool EditorPageController::edgeSnappingEnabled() const
{
    return m_settings->editorEdgeSnappingEnabled();
}
qreal EditorPageController::snapIntervalX() const
{
    return m_settings->editorSnapIntervalX();
}
qreal EditorPageController::snapIntervalY() const
{
    return m_settings->editorSnapIntervalY();
}
int EditorPageController::snapOverrideModifier() const
{
    return m_settings->editorSnapOverrideModifier();
}
bool EditorPageController::fillOnDropEnabled() const
{
    return m_settings->fillOnDropEnabled();
}
int EditorPageController::fillOnDropModifier() const
{
    return m_settings->fillOnDropModifier();
}

void EditorPageController::setDuplicateShortcut(const QString& shortcut)
{
    m_settings->setEditorDuplicateShortcut(shortcut);
}
void EditorPageController::setSplitHorizontalShortcut(const QString& shortcut)
{
    m_settings->setEditorSplitHorizontalShortcut(shortcut);
}
void EditorPageController::setSplitVerticalShortcut(const QString& shortcut)
{
    m_settings->setEditorSplitVerticalShortcut(shortcut);
}
void EditorPageController::setFillShortcut(const QString& shortcut)
{
    m_settings->setEditorFillShortcut(shortcut);
}
void EditorPageController::setGridSnappingEnabled(bool enabled)
{
    m_settings->setEditorGridSnappingEnabled(enabled);
}
void EditorPageController::setEdgeSnappingEnabled(bool enabled)
{
    m_settings->setEditorEdgeSnappingEnabled(enabled);
}
void EditorPageController::setSnapIntervalX(qreal interval)
{
    m_settings->setEditorSnapIntervalX(interval);
}
void EditorPageController::setSnapIntervalY(qreal interval)
{
    m_settings->setEditorSnapIntervalY(interval);
}
void EditorPageController::setSnapOverrideModifier(int mod)
{
    m_settings->setEditorSnapOverrideModifier(mod);
}
void EditorPageController::setFillOnDropEnabled(bool enabled)
{
    m_settings->setFillOnDropEnabled(enabled);
}
void EditorPageController::setFillOnDropModifier(int mod)
{
    m_settings->setFillOnDropModifier(mod);
}

void EditorPageController::resetDefaults()
{
    m_settings->setEditorDuplicateShortcut(ConfigDefaults::editorDuplicateShortcut());
    m_settings->setEditorSplitHorizontalShortcut(ConfigDefaults::editorSplitHorizontalShortcut());
    m_settings->setEditorSplitVerticalShortcut(ConfigDefaults::editorSplitVerticalShortcut());
    m_settings->setEditorFillShortcut(ConfigDefaults::editorFillShortcut());
    m_settings->setEditorGridSnappingEnabled(ConfigDefaults::editorGridSnappingEnabled());
    m_settings->setEditorEdgeSnappingEnabled(ConfigDefaults::editorEdgeSnappingEnabled());
    m_settings->setEditorSnapIntervalX(ConfigDefaults::editorSnapIntervalX());
    m_settings->setEditorSnapIntervalY(ConfigDefaults::editorSnapIntervalY());
    m_settings->setEditorSnapOverrideModifier(ConfigDefaults::editorSnapOverrideModifier());
    m_settings->setFillOnDropEnabled(ConfigDefaults::fillOnDropEnabled());
    m_settings->setFillOnDropModifier(ConfigDefaults::fillOnDropModifier());
}

} // namespace PlasmaZones
