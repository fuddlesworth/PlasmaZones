// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorGeometry/GeometryUtils.h>
#include <PhosphorGeometry/JsonKeys.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace PhosphorGeometry {

QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, const QRect& overlayGeometry)
{
    return QRectF(geometry.x() - overlayGeometry.x(), geometry.y() - overlayGeometry.y(), geometry.width(),
                  geometry.height());
}

QRect snapToRect(const QRectF& rf)
{
    const int left = qRound(rf.x());
    const int top = qRound(rf.y());
    const int right = qRound(rf.x() + rf.width());
    const int bottom = qRound(rf.y() + rf.height());
    return QRect(left, top, std::max(0, right - left), std::max(0, bottom - top));
}

void enforceWindowMinSizes(QVector<QRect>& zones, const QVector<QSize>& minSizes, int gapThreshold, int innerGap)
{
    if (zones.size() != minSizes.size()) {
        return;
    }
    for (int i = 0; i < zones.size(); ++i) {
        QRect& zone = zones[i];
        const QSize& minSize = minSizes[i];
        if (minSize.isEmpty()) {
            continue;
        }
        int needWidth = minSize.width() - zone.width();
        int needHeight = minSize.height() - zone.height();
        if (needWidth <= 0 && needHeight <= 0) {
            continue;
        }
        for (int j = 0; j < zones.size(); ++j) {
            if (i == j)
                continue;
            QRect& neighbor = zones[j];
            bool adjacentH = (std::abs(zone.right() - neighbor.left()) <= gapThreshold
                              || std::abs(neighbor.right() - zone.left()) <= gapThreshold)
                && zone.top() < neighbor.bottom() && zone.bottom() > neighbor.top();
            bool adjacentV = (std::abs(zone.bottom() - neighbor.top()) <= gapThreshold
                              || std::abs(neighbor.bottom() - zone.top()) <= gapThreshold)
                && zone.left() < neighbor.right() && zone.right() > neighbor.left();

            if (needWidth > 0 && adjacentH) {
                int steal = std::min(needWidth, neighbor.width() / 3);
                if (steal <= 0)
                    continue;
                if (zone.right() <= neighbor.left() + gapThreshold) {
                    zone.setRight(zone.right() + steal);
                    neighbor.setLeft(neighbor.left() + steal);
                } else {
                    zone.setLeft(zone.left() - steal);
                    neighbor.setRight(neighbor.right() - steal);
                }
                needWidth -= steal;
            }
            if (needHeight > 0 && adjacentV) {
                int steal = std::min(needHeight, neighbor.height() / 3);
                if (steal <= 0)
                    continue;
                if (zone.bottom() <= neighbor.top() + gapThreshold) {
                    zone.setBottom(zone.bottom() + steal);
                    neighbor.setTop(neighbor.top() + steal);
                } else {
                    zone.setTop(zone.top() - steal);
                    neighbor.setBottom(neighbor.bottom() - steal);
                }
                needHeight -= steal;
            }
        }
    }
    removeZoneOverlaps(zones, minSizes, innerGap);
}

void removeZoneOverlaps(QVector<QRect>& zones, const QVector<QSize>& minSizes, int innerGap)
{
    for (int i = 0; i < zones.size(); ++i) {
        for (int j = i + 1; j < zones.size(); ++j) {
            QRect& a = zones[i];
            QRect& b = zones[j];
            QRect overlap = a.intersected(b);
            if (overlap.isEmpty()) {
                continue;
            }
            int surplusA_w = a.width() - (minSizes.size() > i ? minSizes[i].width() : 0);
            int surplusB_w = b.width() - (minSizes.size() > j ? minSizes[j].width() : 0);
            int surplusA_h = a.height() - (minSizes.size() > i ? minSizes[i].height() : 0);
            int surplusB_h = b.height() - (minSizes.size() > j ? minSizes[j].height() : 0);

            if (overlap.width() < overlap.height()) {
                int mid = overlap.left() + overlap.width() / 2;
                if (surplusA_w >= surplusB_w) {
                    a.setRight(mid - innerGap / 2);
                    b.setLeft(mid + (innerGap + 1) / 2);
                } else {
                    b.setRight(mid - innerGap / 2);
                    a.setLeft(mid + (innerGap + 1) / 2);
                }
            } else {
                int mid = overlap.top() + overlap.height() / 2;
                if (surplusA_h >= surplusB_h) {
                    a.setBottom(mid - innerGap / 2);
                    b.setTop(mid + (innerGap + 1) / 2);
                } else {
                    b.setBottom(mid - innerGap / 2);
                    a.setTop(mid + (innerGap + 1) / 2);
                }
            }
        }
    }
}

QString rectToJson(const QRect& rect)
{
    QJsonObject obj;
    obj[JsonKeys::X] = rect.x();
    obj[JsonKeys::Y] = rect.y();
    obj[JsonKeys::Width] = rect.width();
    obj[JsonKeys::Height] = rect.height();
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace PhosphorGeometry
