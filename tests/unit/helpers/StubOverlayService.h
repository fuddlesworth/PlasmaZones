// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @brief No-op IOverlayService for unit tests that need a constructible
 * WindowDragAdaptor (whose ctor qFatals on a null overlay). Every method is
 * inert; the few queries answer the neutral value ("nothing visible, nothing
 * selected") so drag-adaptor teardown paths walk their default branches.
 */

#include "core/interfaces/ioverlayservice.h"

namespace PlasmaZones::TestHelpers {

class StubOverlayService : public IOverlayService
{
    // Q_OBJECT intentionally omitted: IOverlayService already carries the
    // meta-object; this stub adds no signals or slots of its own (the
    // StubZoneDetector helper documents the same idiom).
public:
    explicit StubOverlayService(QObject* parent = nullptr)
        : IOverlayService(parent)
    {
    }

    bool isVisible() const override
    {
        return false;
    }
    void show() override
    {
    }
    void showAtPosition(int, int) override
    {
    }
    void hide() override
    {
    }
    void toggle() override
    {
    }
    void updateLayout(PhosphorZones::Layout*) override
    {
    }
    void updateSettings(ISettings*) override
    {
    }
    void updateGeometries() override
    {
    }
    void highlightZone(const QString&) override
    {
    }
    void highlightZones(const QStringList&) override
    {
    }
    void clearHighlight() override
    {
    }
    void setIdleForDragPause() override
    {
    }
    void forgetCurrentScreen() override
    {
    }
    void refreshFromIdle() override
    {
    }
    void updateScrollDropIndicator(const QString&, const QRect&, bool) override
    {
    }
    void setScrollDropIndicatorWindowOverrides(const QVariantMap&) override
    {
    }
    bool isZoneSelectorVisible() const override
    {
        return false;
    }
    void showZoneSelector(const QString&) override
    {
    }
    void hideZoneSelector() override
    {
    }
    void updateSelectorPosition(int, int) override
    {
    }
    void scrollZoneSelector(int) override
    {
    }
    void updateMousePosition(int, int) override
    {
    }
    int visibleLayoutCount(const QString&) const override
    {
        return 0;
    }
    bool screenResolvesToTemplates(const QString&) const override
    {
        return false;
    }
    int selectorCardCount(const QString&) const override
    {
        return 0;
    }
    QList<qreal> selectorStripFractions(const QString&) const override
    {
        return {};
    }
    bool hasSelectedZone() const override
    {
        return false;
    }
    QString selectedLayoutId() const override
    {
        return {};
    }
    int selectedZoneIndex() const override
    {
        return -1;
    }
    QRect getSelectedZoneGeometry(QScreen*) const override
    {
        return {};
    }
    QRect getSelectedZoneGeometry(const QString&) const override
    {
        return {};
    }
    void clearSelectedZone() override
    {
    }
    void showShaderPreview(int, int, int, int, const QString&, const QString&, const QString&, const QString&) override
    {
    }
    void updateShaderPreview(int, int, int, int, const QString&, const QString&) override
    {
    }
    void hideShaderPreview() override
    {
    }
    void showSnapAssist(const QString&, const PhosphorProtocol::EmptyZoneList&,
                        const PhosphorProtocol::SnapAssistCandidateList&) override
    {
    }
    void hideSnapAssist() override
    {
    }
    bool isSnapAssistVisible() const override
    {
        return false;
    }
    bool setSnapAssistThumbnail(const QString&, int, int, const QByteArray&) override
    {
        return false;
    }
    bool setWindowThumbnailDmabuf(const QString&, const DmabufThumbnailDesc&) override
    {
        return false;
    }
    void hideLayoutPicker() override
    {
    }
    bool isLayoutPickerVisible() const override
    {
        return false;
    }
    void hideCheatsheet() override
    {
    }
    bool isCheatsheetVisible() const override
    {
        return false;
    }
};

} // namespace PlasmaZones::TestHelpers
