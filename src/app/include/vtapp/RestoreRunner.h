#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QProcess;

#include "RestorePlan.h"

namespace vtapp {

/// 修复导出的执行器。
///
/// **执行模型**：所有修复阶段串成一条滤镜链、在**一次** ffmpeg 遍里完成，
/// 而不是每个阶段起一个进程。理由是后者要为每个阶段落一次无损中间文件，
/// 磁盘与耗时都不可接受；而滤镜链本来就是 ffmpeg 的原生表达。
/// 因此真正需要独立进程的只有三种情况：
///   · 稳定阶段的**探测遍**（vidstabdetect 必须先写出变换文件）
///   · 主遍（滤镜链 + 编码）
///   · 单帧预览
///
/// 取消、临时目录、空间校验都集中在这里；`RestorePlan` 只负责"该做什么"。
class RestoreRunner : public QObject {
    Q_OBJECT
public:
    explicit RestoreRunner(QObject* parent = nullptr);
    ~RestoreRunner() override;

    // 状态都放在 Impl 里（头文件不需要暴露实现细节），
    // 因此这几个访问器只在 .cpp 里定义。
    void setTranscoderPath(const QString& path);
    QString transcoderPath() const;
    void setEncoderThreads(int n);

    bool isRunning() const;

    /// 只构造命令并发出 `commandBuilt`，不真正执行。
    /// 修复导出的参数由多级推导而来，逐级排查时不必每次都等一次完整导出
    /// （保真档很慢，一次完整导出可能要几分钟）。
    void setDryRun(bool dry);

    /// 开始导出。`plan.valid` 必须为 true；时间段必须已通过 `validateTimeRange`。
    /// `durationSec <= 0` 表示时长未知（进度比例将不可用）。
    void start(const RestorePlan& plan, const QString& input, const QString& output,
               int srcW, int srcH, bool hasAudio, double durationSec,
               const TimeRange& range, qint64 expectedBytes);

    /// 单帧预览：把 `atSec` 处的画面按预览阶段清单渲染成 PNG。
    ///
    /// **同步**执行（实测 < 1 秒）：这样调用方可以在按钮点击里直接拿到结果，
    /// 不需要为"预览"也建一套异步状态机。
    /// 返回空串表示成功且路径写入 `outPng`，否则返回错误说明。
    QString renderPreview(const RestorePlan& plan, const QString& input, double atSec,
                          int srcW, int srcH, const QString& outPng);

    /// 取消：终止当前阶段子进程 → 删除半成品输出 → 删除临时目录。
    void cancel();

    /// 清理因异常退出而残留的临时目录。
    /// 只在**启动时**调用：按时间阈值判断，避免误删正在运行的另一实例的目录。
    static void sweepStaleTempDirs(int olderThanHours = 6);

    QString lastErrorDetail() const;
    /// 当前阶段序号与名称（用于界面展示）。
    int     currentStageIndex() const;

signals:
    /// 每次构造出实际命令时发出。
    /// 修复导出的参数由「阶段清单 → 滤镜链 → 参数」多级推导而来，
    /// 出问题时唯一能直接回答"到底执行了什么"的就是这条命令本身。
    /// 用信号而不是日志：调用方可以选择打日志、显示、或断言，且不受日志等级影响。
    void commandBuilt(const QString& pass, const QString& command);

    /// ratio 0..1（时长未知为 -1）；stageIndex/stageName 说明当前在做什么。
    void progressChanged(double ratio, double speed, qint64 writtenBytes,
                         int stageIndex, const QString& stageName);
    void finished(const QString& outputPath);
    void failed(const QString& message, const QString& detail);
    void cancelled();
    void runningChanged();

private:
    void runDetectPass();     ///< 稳定阶段的探测遍（若需要）
    void runMainPass();       ///< 主遍
    void onProcessFinished(int exitCode);
    void onStdout();
    void failWith(const QString& message, const QString& detail);
    void cleanupTempDir();
    QString buildMainChain() const;   ///< 滤镜链 + 时间段裁剪

    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace vtapp
