// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationshaderitem.h"
#include "animationshadernoderhi.h"

#include <QFile>
#include <QQuickWindow>
#include <QSGTextureProvider>

namespace PlasmaZones {

AnimationShaderItem::AnimationShaderItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    for (auto& p : m_customParams)
        p = QVector4D(-1.0f, -1.0f, -1.0f, -1.0f);
}

AnimationShaderItem::~AnimationShaderItem()
{
    if (m_renderNode && window()) {
        m_renderNode->invalidateItem();
    }
    m_renderNode = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// updatePaintNode — sync properties to render node (called on render thread)
// ═══════════════════════════════════════════════════════════════════════════════

QSGNode* AnimationShaderItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    if (width() <= 0 || height() <= 0 || !m_source || !window() || m_shaderSource.isEmpty()) {
        delete oldNode;
        m_renderNode = nullptr;
        return nullptr;
    }

    auto* node = static_cast<AnimationShaderNodeRhi*>(oldNode);
    if (!node) {
        node = new AnimationShaderNodeRhi(this);
        m_renderNode = node;
        m_shaderDirty = true; // Force shader load on first creation
    }

    // Load shader from disk when source URL changes
    if (m_shaderDirty.exchange(false)) {
        const QString fragPath = m_shaderSource.toLocalFile();
        if (!fragPath.isEmpty() && QFile::exists(fragPath)) {
            if (!node->loadFragmentShader(fragPath)) {
                setError(node->shaderError());
            }
        } else if (!fragPath.isEmpty()) {
            setError(QStringLiteral("Animation shader not found: ") + fragPath);
        }
    }

    // Sync animation state
    node->setProgress(static_cast<float>(m_progress));
    node->setDuration(static_cast<float>(m_duration));
    node->setStyleParam(static_cast<float>(m_styleParam));
    node->setDirection(m_direction);
    node->setResolution(static_cast<float>(width()), static_cast<float>(height()));

    // Sync custom params
    for (int i = 0; i < 8; ++i)
        node->setCustomParams(i, m_customParams[i]);

    // Get source texture from the source item's texture provider (layer)
    if (m_source->isTextureProvider()) {
        QSGTextureProvider* provider = m_source->textureProvider();
        if (provider) {
            node->setSourceTexture(provider->texture());
        }
    }

    // Check shader status
    if (node->isShaderReady()) {
        if (m_status != Ready)
            setStatus(Ready);
    } else if (!node->shaderError().isEmpty()) {
        setError(node->shaderError());
    }

    node->markDirty(QSGNode::DirtyMaterial);
    return node;
}

void AnimationShaderItem::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickItem::itemChange(change, value);

    if (change == ItemVisibleHasChanged && value.boolValue) {
        m_shaderDirty = true;
        update();
    }
    if (change == ItemSceneChange && value.window) {
        connect(
            value.window, &QQuickWindow::sceneGraphAboutToStop, this,
            [this]() {
                if (m_renderNode)
                    m_renderNode->releaseResources();
                m_shaderDirty = true;
            },
            Qt::DirectConnection);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Property accessors
// ═══════════════════════════════════════════════════════════════════════════════

QQuickItem* AnimationShaderItem::source() const
{
    return m_source;
}

void AnimationShaderItem::setSource(QQuickItem* source)
{
    if (m_source == source)
        return;
    m_source = source;
    Q_EMIT sourceChanged();
    update();
}

QUrl AnimationShaderItem::shaderSource() const
{
    return m_shaderSource;
}

void AnimationShaderItem::setShaderSource(const QUrl& source)
{
    if (m_shaderSource == source)
        return;
    m_shaderSource = source;
    m_shaderDirty = true;
    Q_EMIT shaderSourceChanged();
    if (source.isEmpty()) {
        setStatus(Null);
    } else {
        setStatus(Loading);
    }
    update();
}

qreal AnimationShaderItem::progress() const
{
    return m_progress;
}

void AnimationShaderItem::setProgress(qreal progress)
{
    if (qFuzzyCompare(m_progress, progress))
        return;
    m_progress = progress;
    Q_EMIT progressChanged();
    update();
}

qreal AnimationShaderItem::duration() const
{
    return m_duration;
}

void AnimationShaderItem::setDuration(qreal duration)
{
    if (qFuzzyCompare(m_duration, duration))
        return;
    m_duration = duration;
    Q_EMIT durationChanged();
    update();
}

qreal AnimationShaderItem::styleParam() const
{
    return m_styleParam;
}

void AnimationShaderItem::setStyleParam(qreal param)
{
    if (qFuzzyCompare(m_styleParam, param))
        return;
    m_styleParam = param;
    Q_EMIT styleParamChanged();
    update();
}

int AnimationShaderItem::direction() const
{
    return m_direction;
}

void AnimationShaderItem::setDirection(int direction)
{
    if (m_direction == direction)
        return;
    m_direction = direction;
    Q_EMIT directionChanged();
    update();
}

QVariantMap AnimationShaderItem::shaderParams() const
{
    return m_shaderParams;
}

void AnimationShaderItem::setShaderParams(const QVariantMap& params)
{
    if (m_shaderParams == params)
        return;
    m_shaderParams = params;

    // Extract customParams from the map (same format as ZoneShaderItem)
    for (int vec = 0; vec < 8; ++vec) {
        const QString prefix = QStringLiteral("customParams%1_").arg(vec + 1);
        static const QChar components[] = {QLatin1Char('x'), QLatin1Char('y'), QLatin1Char('z'), QLatin1Char('w')};
        for (int c = 0; c < 4; ++c) {
            const QString key = prefix + components[c];
            auto it = params.find(key);
            if (it != params.end()) {
                float val = it->toFloat();
                switch (c) {
                case 0:
                    m_customParams[vec].setX(val);
                    break;
                case 1:
                    m_customParams[vec].setY(val);
                    break;
                case 2:
                    m_customParams[vec].setZ(val);
                    break;
                case 3:
                    m_customParams[vec].setW(val);
                    break;
                }
            }
        }
    }

    Q_EMIT shaderParamsChanged();
    update();
}

AnimationShaderItem::Status AnimationShaderItem::status() const
{
    return m_status;
}

QString AnimationShaderItem::errorLog() const
{
    return m_errorLog;
}

void AnimationShaderItem::setStatus(Status status)
{
    if (m_status == status)
        return;
    m_status = status;
    Q_EMIT statusChanged();
    if (status != Error && !m_errorLog.isEmpty()) {
        m_errorLog.clear();
        Q_EMIT errorLogChanged();
    }
}

void AnimationShaderItem::setError(const QString& error)
{
    m_status = Error;
    m_errorLog = error;
    Q_EMIT statusChanged();
    Q_EMIT errorLogChanged();
}

} // namespace PlasmaZones
