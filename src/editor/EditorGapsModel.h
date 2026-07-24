// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QVariantMap>

namespace PlasmaZones {

class EditorController;

/**
 * @brief Per-layout gap override sub-model for the layout editor.
 *
 * Owns the zone padding and edge (outer) gap override state that used to live
 * directly on EditorController: the per-layout override values (-1 = fall back
 * to the global setting) plus the cached global mirrors read from the daemon.
 * Exposed to QML as EditorController's @c gaps property so bindings read
 * @c controller.gaps.outerGapTop and friends.
 *
 * Holds a back-pointer to its owning EditorController to push undo commands
 * onto the shared stack and mark the layout dirty — the same coupling the
 * editor's undo commands already have with the controller.
 */
class EditorGapsModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int zonePadding READ zonePadding WRITE setZonePadding NOTIFY zonePaddingChanged)
    Q_PROPERTY(int outerGap READ outerGap WRITE setOuterGap NOTIFY outerGapChanged)
    Q_PROPERTY(bool hasZonePaddingOverride READ hasZonePaddingOverride NOTIFY zonePaddingChanged)
    Q_PROPERTY(bool hasOuterGapOverride READ hasOuterGapOverride NOTIFY outerGapChanged)
    Q_PROPERTY(int globalZonePadding READ globalZonePadding NOTIFY globalZonePaddingChanged)
    Q_PROPERTY(int globalOuterGap READ globalOuterGap NOTIFY globalOuterGapChanged)

    // Per-side outer gap overrides
    Q_PROPERTY(bool usePerSideOuterGap READ usePerSideOuterGap WRITE setUsePerSideOuterGap NOTIFY outerGapChanged)
    Q_PROPERTY(int outerGapTop READ outerGapTop WRITE setOuterGapTop NOTIFY outerGapChanged)
    Q_PROPERTY(int outerGapBottom READ outerGapBottom WRITE setOuterGapBottom NOTIFY outerGapChanged)
    Q_PROPERTY(int outerGapLeft READ outerGapLeft WRITE setOuterGapLeft NOTIFY outerGapChanged)
    Q_PROPERTY(int outerGapRight READ outerGapRight WRITE setOuterGapRight NOTIFY outerGapChanged)
    Q_PROPERTY(bool globalUsePerSideOuterGap READ globalUsePerSideOuterGap NOTIFY globalOuterGapChanged)
    Q_PROPERTY(int globalOuterGapTop READ globalOuterGapTop NOTIFY globalOuterGapChanged)
    Q_PROPERTY(int globalOuterGapBottom READ globalOuterGapBottom NOTIFY globalOuterGapChanged)
    Q_PROPERTY(int globalOuterGapLeft READ globalOuterGapLeft NOTIFY globalOuterGapChanged)
    Q_PROPERTY(int globalOuterGapRight READ globalOuterGapRight NOTIFY globalOuterGapChanged)

public:
    explicit EditorGapsModel(EditorController* controller, QObject* parent = nullptr);

    // Property getters
    int zonePadding() const;
    int outerGap() const;
    bool hasZonePaddingOverride() const;
    bool hasOuterGapOverride() const;
    int globalZonePadding() const;
    int globalOuterGap() const;
    bool usePerSideOuterGap() const;
    int outerGapTop() const;
    int outerGapBottom() const;
    int outerGapLeft() const;
    int outerGapRight() const;
    bool globalUsePerSideOuterGap() const;
    int globalOuterGapTop() const;
    int globalOuterGapBottom() const;
    int globalOuterGapLeft() const;
    int globalOuterGapRight() const;

    // Property setters (create undo commands)
    void setZonePadding(int padding);
    void setOuterGap(int gap);
    void setUsePerSideOuterGap(bool enabled);
    void setOuterGapTop(int gap);
    void setOuterGapBottom(int gap);
    void setOuterGapLeft(int gap);
    void setOuterGapRight(int gap);
    Q_INVOKABLE void clearZonePaddingOverride();
    Q_INVOKABLE void clearOuterGapOverride();

    // Direct setters (for undo/redo, bypass command creation)
    void setZonePaddingDirect(int padding);
    void setOuterGapDirect(int gap);
    void setUsePerSideOuterGapDirect(bool enabled);
    void setOuterGapTopDirect(int gap);
    void setOuterGapBottomDirect(int gap);
    void setOuterGapLeftDirect(int gap);
    void setOuterGapRightDirect(int gap);

    // Controller-facing helpers for layout load/save and startup.
    /// Reset every per-layout override to "use global" (-1 / false) without
    /// emitting — the caller emits the change signals once the whole layout
    /// swap has settled (see EditorController::createNewLayout).
    void resetOverrides();
    /// Read the per-layout override keys out of a layout JSON object, then emit
    /// zonePaddingChanged (only if it changed) and outerGapChanged (always —
    /// per-side values can differ between layouts even when the uniform gap is
    /// numerically unchanged).
    void loadFromJson(const QJsonObject& layoutObj);
    /// Serialize the per-layout overrides that are actually set (>= 0, or the
    /// per-side toggle when enabled) into a layout JSON object.
    void writeToJson(QJsonObject& layoutObj) const;
    /// Refresh the cached global mirrors from a batched daemon settings reply,
    /// emitting globalZonePaddingChanged / globalOuterGapChanged on change.
    void applyGlobalSettings(const QVariantMap& values);
    /// Emit zonePaddingChanged + outerGapChanged unconditionally. Used after a
    /// bulk reset where the caller deferred notification.
    void emitOverrideSignals();

Q_SIGNALS:
    void zonePaddingChanged();
    void outerGapChanged();
    void globalZonePaddingChanged();
    void globalOuterGapChanged();

private:
    EditorController* m_controller;

    // Per-layout overrides (-1 = use global)
    int m_zonePadding = -1;
    int m_outerGap = -1;
    bool m_usePerSideOuterGap = false;
    int m_outerGapTop = -1;
    int m_outerGapBottom = -1;
    int m_outerGapLeft = -1;
    int m_outerGapRight = -1;

    // Cached global mirrors (avoid D-Bus calls in property reads)
    int m_cachedGlobalZonePadding;
    int m_cachedGlobalOuterGap;
    bool m_cachedGlobalUsePerSideOuterGap = false;
    int m_cachedGlobalOuterGapTop;
    int m_cachedGlobalOuterGapBottom;
    int m_cachedGlobalOuterGapLeft;
    int m_cachedGlobalOuterGapRight;
};

} // namespace PlasmaZones
