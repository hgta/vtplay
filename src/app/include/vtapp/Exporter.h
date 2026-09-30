#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

class QProcess;

namespace vtapp {

/// 导出预设：面向"能发出去"的分享场景，用「短边 + 码率」表达，
/// 而不是固定分辨率——这样竖屏/横屏/异形比例都能自动适配。
struct ExportPreset {
    QString id;                 // 稳定标识（ASCII，用于持久化与命令行）
    QString label;              // 界面显示名
    QString detail;             // 一行说明（分辨率/码率/用途）
    int     shortSide  = 0;     // 短边上限（0 = 不缩放）
    int     videoKbps  = 0;     // 视频码率上限（kbps）
    int     crf        = 23;    // 质量上限（越小越好）
    int     audioKbps  = 128;   // 音频码率（kbps）
    int     maxSeconds = 0;     // >0：仅导出前 N 秒
    bool    streamCopy = false; // 仅换容器（不重新编码视频）
    QString suffix;             // 输出文件名后缀标签
};

/// 外部转码器（ffmpeg）的可用性信息。
struct TranscoderInfo {
    bool    available = false;
    QString path;       // 可执行文件绝对路径
    QString version;    // 版本首行
    QString source;     // 探测来源：configured / appdir / PATH
};

/// 视频导出器：以**外部 ffmpeg 子进程**完成转码。
///
/// 设计取舍见 openspec/changes/add-video-export/design.md（决策 D1）：
/// 应用不链接编码 API，进程隔离使导出与播放互不干扰，也为将来切换到
/// LGPL 构建的 FFmpeg 保留余地。所有参数通过 QStringList 传递（不经 shell），
/// 因此中文路径与文件名天然安全。
class Exporter : public QObject {
    Q_OBJECT
public:
    explicit Exporter(QObject* parent = nullptr);
    ~Exporter() override;

    // ---- 预设 ----
    static QList<ExportPreset> builtinPresets();
    static ExportPreset presetById(const QString& id);

    // ---- 纯逻辑（无副作用，可 headless 验证）----
    /// 缩放滤镜表达式；源短边已 <= 目标时返回空串（**不放大**）。
    /// 竖屏用 `scale=S:-2`、横屏用 `scale=-2:S`，长边由 -2 保证偶数
    /// （H.264 要求偶数尺寸）。
    static QString scaleFilter(int srcW, int srcH, int shortSide);

    /// 构造 ffmpeg 参数列表（不含可执行文件本身）。
    /// threads > 0 时限制编码线程数，用于避免与播放解码争抢 CPU。
    static QStringList buildArgs(const ExportPreset& preset,
                                 const QString& input, const QString& output,
                                 int srcW, int srcH, bool hasAudio, int threads = 0);

    /// 预估输出体积（字节）。streamCopy 时返回源文件大小（体积基本不变）。
    static qint64 estimateBytes(const ExportPreset& preset, double durationSec,
                                qint64 sourceBytes);

    /// 生成输出路径：与源同目录、`<base>_<后缀>.mp4`；已存在时追加 `_1`/`_2`，
    /// **绝不覆盖**既有文件。
    static QString makeOutputPath(const QString& inputPath, const QString& suffix);

    /// 目标目录可用空间是否足够（要求 > 预估体积 × 1.2 的余量）。
    /// 无法判断存储信息时返回 true（不阻止导出）。
    static bool hasEnoughSpace(const QString& dirPath, qint64 requiredBytes);

    // ---- 转码器探测 ----
    /// 三级探测：用户配置路径 → 应用同级目录（含 bin/） → 系统 PATH。
    /// 命中后会执行一次 `-version` 验证可执行性并读取版本。
    static TranscoderInfo probeTranscoder(const QString& configuredPath = QString());

    // ---- 执行 ----
    bool isRunning() const { return running_; }
    void setTranscoderPath(const QString& path) { transcoderPath_ = path; }
    QString transcoderPath() const { return transcoderPath_; }
    void setEncoderThreads(int n) { encoderThreads_ = n; }

    /// 启动导出。durationSec 用于计算进度比例（<=0 时比例返回 -1）；
    /// expectedBytes > 0 时先校验目标目录可用空间，不足则直接失败而不启动转码。
    void start(const ExportPreset& preset, const QString& input, const QString& output,
               int srcW, int srcH, bool hasAudio, double durationSec,
               qint64 expectedBytes = 0);

    /// 取消导出：终止进程并删除半成品文件。
    void cancel();

    /// 最近一次失败时保留的转码器输出尾部（用于诊断展示）。
    QString lastErrorDetail() const { return errorDetail_; }

signals:
    /// 进度更新：ratio 为 0..1（时长未知时为 -1）；speed 为倍率（未知为 0）。
    void progressChanged(double ratio, double speed, qint64 writtenBytes);
    void finished(const QString& outputPath);
    void failed(const QString& message, const QString& detail);
    void cancelled();
    void runningChanged();

private:
    void onStdout();
    void onStderr();
    void onFinished(int exitCode);
    void discardPartialOutput();

    std::unique_ptr<QProcess> proc_;
    QString transcoderPath_;
    QString outputPath_;
    double  durationSec_ = 0.0;
    bool    running_         = false;
    bool    cancelRequested_ = false;
    int     encoderThreads_  = 0;
    // 进度解析状态
    QString outBuf_;
    double  outTimeSec_   = 0.0;
    double  speed_        = 0.0;
    qint64  writtenBytes_ = 0;
    // 失败诊断（stderr 尾部）
    QString errorDetail_;
    QList<QString> errTail_;
};

} // namespace vtapp
