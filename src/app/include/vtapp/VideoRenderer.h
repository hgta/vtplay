#pragma once

#include <QQuickItem>
#include <QImage>
#include <QTimer>
#include <QtQml>

namespace vtcore { class MediaPipeline; }

namespace vtapp {

/// QML 视频渲染项。内置帧抓取定时器，每 ~16ms 从管线拉取最新视频帧
/// 并显示为 QImage 源。等同于 MVP 阶段的"输出层"；未来可替换为原生 QSG 纹理。
class VideoRenderer : public QQuickItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(VideoRenderer)
public:
    explicit VideoRenderer(QQuickItem* parent = nullptr);
    void attach(vtcore::MediaPipeline* pipeline);

    /// 当前显示帧的副本（**纯画面**：不含界面元素，也不含 letterbox 黑边）。
    /// 尺寸为解码输出尺寸（≈ 渲染区物理像素），因此比抓屏更干净、无锯齿黑边。
    /// 无帧时返回空 QImage。
    ///
    /// 线程安全：image_ 只在渲染线程的 updatePaintNode 里被替换，而那一阶段
    /// GUI 线程是被同步阻塞的，所以从 GUI 线程调用本函数不会撞上写入；
    /// 且替换是整体 move（不是原地改内存），取到旧引用也依然有效。
    QImage currentFrameImage() const { return image_; }

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    /// 按视频宽高比在 item 内计算的显示矩形（保持比例，留黑边）。
    QRectF fittedRect() const;

private slots:
    void onTick();

private:
    vtcore::MediaPipeline* pipeline_ = nullptr;
    QTimer timer_;
    // 跨线程帧传递：GUI 线程写 pendingImage_；渲染线程在 updatePaintNode
    // 开头（此阶段 GUI 线程被同步阻塞）把它交换到 image_ 后再建纹理。
    QImage pendingImage_;   // 仅 GUI 线程写
    QImage image_;          // 仅渲染线程读写
    int lastTargetW_ = 0;   // 已上报给管线的解码输出尺寸
    int lastTargetH_ = 0;
};

} // namespace vtapp