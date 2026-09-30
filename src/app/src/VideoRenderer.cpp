#include "VideoRenderer.h"

#include "MediaPipeline.h"
#include "VideoFrame.h"

#include <QSGNode>
#include <QSGTexture>
#include <QSGSimpleTextureNode>
#include <QQuickWindow>

namespace vtapp {

VideoRenderer::VideoRenderer(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(QQuickItem::ItemHasContents, true);
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, &VideoRenderer::onTick);
    timer_.start();
}

void VideoRenderer::attach(vtcore::MediaPipeline* p) {
    pipeline_ = p;
}

void VideoRenderer::onTick() {
    if (!pipeline_) return;

    // 把渲染区尺寸告诉管线：解码时直接缩放到可用尺寸，
    // 避免 4K 源全尺寸转换/上传（4K 播放性能的关键）。
    const QSizeF s = size();
    // 解码尺寸必须按**物理像素**计算（逻辑尺寸 × devicePixelRatio）。
    // 高 DPI 屏（如 150%）上，若只按逻辑尺寸解码，纹理会被 QSG 放大 1.5 倍
    // 显示 → 画面明显模糊（这是"本地播放不如原片清晰"的根本原因）。
    // 临时：VTPLAY_IGNORE_DPR=1 复现修复前的行为（用于画质对比验证）
    const qreal dpr = window() ? window()->devicePixelRatio() : 1.0;
    if (s.width() >= 2.0 && s.height() >= 2.0) {
        const int w = static_cast<int>(s.width()  * dpr);
        const int h = static_cast<int>(s.height() * dpr);
        if (w != lastTargetW_ || h != lastTargetH_) {
            lastTargetW_ = w;
            lastTargetH_ = h;
            pipeline_->setVideoTargetSize(w, h);
        }
    }

    // 按媒体时钟取"该显示了"的那一帧（音视频同步）。
    const double mediaSec = pipeline_->positionSec();
    const bool draining = pipeline_->isDemuxDone();

    // 取帧容差：播放中放宽 20ms 吸收时钟抖动；暂停/逐帧时**不放宽**。
    // 逐帧步进尤其依赖这一点——容差比一帧（60fps 下 16.7ms）还大，
    // 会让「前进一帧」顺手把下一帧也吃掉，表现为偶尔跳两帧。
    const double tol = (pipeline_->status() == vtcore::PlayerStatus::Playing) ? 0.02 : 0.0;

    std::optional<vtcore::VideoFrame> chosen;
    if (draining) {
        // 结尾 drain：允许超前取帧，每 tick 取一帧，让末尾数帧也能依次显示。
        chosen = pipeline_->takeVideoUpTo(mediaSec + tol, true);
    } else {
        // 正常播放：把到点的帧全部取走，只显示最后一个，避免积压导致延迟增长。
        while (auto v = pipeline_->takeVideoUpTo(mediaSec + tol, false)) {
            chosen = std::move(v);
        }
    }
    if (!chosen || !chosen->valid) return;

    QImage img(chosen->rgba.data(), chosen->width, chosen->height,
               chosen->stride, QImage::Format_RGBA8888);
    if (img.isNull()) return;
    // 只写 pending，渲染线程会在同步点交换；绝不在渲染进行中改 image_
    pendingImage_ = img.copy();
    update();
}

QSGNode* VideoRenderer::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    // 本函数运行在 QSG 渲染线程，执行期间 GUI 线程被阻塞，
    // 因此在此交换 pendingImage_ 是线程安全的。
    if (!pendingImage_.isNull()) {
        image_ = std::move(pendingImage_);
        pendingImage_ = QImage();
    }

    if (image_.isNull()) {
        // 尚未收到任何帧：返回空节点（避免 "No QSGTexture provided" 警告）
        delete oldNode;
        return nullptr;
    }

    auto* node = dynamic_cast<QSGSimpleTextureNode*>(oldNode);
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(true);
    }

    auto* win = window();
    QSGTexture* newTex = win ? win->createTextureFromImage(image_) : nullptr;
    if (!newTex) {
        // 创建失败（如设备丢失）：保留旧纹理，下一帧重试
        return node;
    }

    // 注意：ownsTexture == true 时 setTexture() 内部会自动 delete 旧纹理，
    // 调用方绝不能再次手动 delete，否则 double free 破坏堆（曾导致 4K 播放崩溃）。
    node->setTexture(newTex);
    node->setRect(fittedRect());
    return node;
}

/// 按视频原始宽高比适配到当前 item 尺寸（letterbox / pillarbox），
/// 避免把画面拉伸变形。
QRectF VideoRenderer::fittedRect() const {
    const QRectF item = boundingRect();
    if (image_.isNull() || image_.height() <= 0 || item.height() <= 0.0)
        return item;

    const double imgAspect  = double(image_.width()) / double(image_.height());
    const double itemAspect = item.width() / item.height();

    if (imgAspect > itemAspect) {
        // 视频更宽：宽度铺满，上下留黑边
        const double h = item.width() / imgAspect;
        return QRectF(item.x(), item.y() + (item.height() - h) / 2.0, item.width(), h);
    }
    // 视频更高：高度铺满，左右留黑边
    const double w = item.height() * imgAspect;
    return QRectF(item.x() + (item.width() - w) / 2.0, item.y(), w, item.height());
}

} // namespace vtapp