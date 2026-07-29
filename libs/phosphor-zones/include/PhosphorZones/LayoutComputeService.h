// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorZones/LayoutComputeTypes.h>
#include <phosphorzones_export.h>
#include <QHash>
#include <QObject>
#include <QPointer>

class QThread;

namespace PhosphorZones {

class Layout;
class LayoutRegistry;
class LayoutWorker;

class PHOSPHORZONES_EXPORT LayoutComputeService : public QObject
{
    Q_OBJECT

public:
    explicit LayoutComputeService(QObject* parent = nullptr);
    ~LayoutComputeService() override;

    void setLayoutManager(LayoutRegistry* manager);

    bool requestRecalculate(Layout* layout, const QString& screenId, const QRectF& screenGeometry);
    uint64_t currentGeneration(const QString& screenId) const;

    static void recalculateSync(Layout* layout, const QRectF& screenGeometry);

Q_SIGNALS:
    void requestCompute(const PhosphorZones::LayoutSnapshot& snapshot, uint64_t generation);
    void geometriesComputedForGeneration(const QString& screenId, const QUuid& layoutId, PhosphorZones::Layout* layout,
                                         uint64_t generation);

private:
    static LayoutSnapshot buildSnapshot(Layout* layout, const QString& screenId, const QRectF& screenGeometry);
    void applyResult(const LayoutComputeResult& result);
    void onLayoutRemoved(const QUuid& layoutId);
    void publishResult(const QString& screenId, const QUuid& layoutId, Layout* layout, uint64_t generation);

    QHash<QUuid, QPointer<Layout>> m_trackedLayouts;
    QHash<QString, uint64_t> m_screenGeneration;
    QPointer<LayoutRegistry> m_layoutManager;
    QThread* m_thread = nullptr;
    LayoutWorker* m_worker = nullptr;
};

} // namespace PhosphorZones
