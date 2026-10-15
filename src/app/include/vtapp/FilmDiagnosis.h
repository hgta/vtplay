#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class QProcess;
class QTimer;

namespace vtapp {

/// 诊断分级。
///
/// 刻意把 Unknown 与 None 分开：把「未知」当成「正常」会让后续决策建立在错误前提上
/// （例如误认为源是逐行的 → 不做去隔行 → 梳状纹被放大固化，再也去不掉）。
/// 因此「未知」必须一路传到决策层，由决策层按最保守方式处理。
enum class DiagLevel {
    Unknown = -1,
    None    = 0,
    Mild    = 1,
    Obvious = 2,
};

/// 扫描方式。交错与 pulldown 不是"强度"而是"类型"，所以单独枚举。
enum class ScanType {
    Unknown,
    Progressive,
    InterlacedTff,   ///< 交错，上场优先
    InterlacedBff,   ///< 交错，下场优先
    Pulldown32,      ///< 3:2 pulldown（DVD 电影/美剧常见，应走反胶转而非去隔行）
};

/// 分级 / 扫描方式的中文短名，直接可显示。
QString diagLevelName(DiagLevel v);
QString scanTypeName(ScanType t);

/// 一次源诊断的完整结论。
///
/// 对外只给**分级结论**（无 / 轻度 / 明显 / 未知），原始数值一并保留——
/// 分级阈值需要按实际素材标定，标定与排查都离不开原始值。
struct DiagnosisResult {
    // ---- 扫描方式（idet）----
    ScanType scan               = ScanType::Unknown;
    double   interlacedRatio    = -1.0;   ///< 交错帧占比 0..1
    double   repeatedFieldRatio = -1.0;   ///< 重复场占比（pulldown 线索）
    bool     scanOverridden     = false;  ///< 是否由用户手动指定（而非检出）

    // ---- 高频细节量（gblur 差分残差均值）----
    // 这是**代理指标**：它度量画面的高频能量，既包含噪点/颗粒，也包含真实纹理，
    // 无法在只依赖 ffmpeg 的前提下把两者分开。因此它用于比较与分级（同源纵向、
    // 不同源横向），而不是当作"噪声绝对值"使用。bitplanenoise 的数值读不到
    // （它只写日志通道，实测连 trace 等级都取不到），故采用此法。
    DiagLevel detail      = DiagLevel::Unknown;
    double    detailValue = -1.0;

    // ---- 抖动（vidstabdetect 结果文件）----
    // 指标为全局位移的**二阶差分**标准差：平滑摇镜的二阶差分接近 0，不算抖动；
    // 抖动正是位移的高频分量。单位为「像素/帧²」。
    DiagLevel shake      = DiagLevel::Unknown;
    double    shakeValue = -1.0;

    // ---- 黑边（cropdetect）----
    // 同样是分级而非布尔：拿不到源尺寸时**判为未知**，不能报「无」——
    // 报「无」会让上层以为确认过没有黑边。
    DiagLevel bars = DiagLevel::Unknown;
    int  cropW = 0, cropH = 0, cropX = 0, cropY = 0;
    int  srcW  = 0, srcH  = 0;

    // ---- 亮度 / 色彩（signalstats）----
    DiagLevel lumaShift   = DiagLevel::Unknown;
    DiagLevel chromaShift = DiagLevel::Unknown;
    double    lumaMin = -1.0, lumaMax = -1.0, lumaAvg = -1.0, satAvg = -1.0;

    // ---- 元信息 ----
    double   analyzedSeconds = 0.0;   ///< 实际分析的时长（采样合计）
    int      segments        = 0;     ///< 实际采样的段数
    QStringList notes;                ///< 哪些项降级为未知、以及原因

    /// 是否至少有一项可用。全不可用时决策层必须按最保守方式处理。
    bool anyUsable() const;
};

/// 老片源诊断。
///
/// 与 `Exporter` 同层（都是 QProcess 驱动外部 ffmpeg），但**独立于导出**：
/// 诊断本身就有价值——用户可以先知道"我这个源到底怎么了"，再决定要不要修复。
///
/// 全部诊断项仅使用随包 ffmpeg 已有滤镜，不引入任何新依赖。
class FilmDiagnosis : public QObject {
    Q_OBJECT
public:
    explicit FilmDiagnosis(QObject* parent = nullptr);
    ~FilmDiagnosis() override;

    void setTranscoderPath(const QString& path) { transcoderPath_ = path; }
    QString transcoderPath() const { return transcoderPath_; }

    /// 采样策略：沿时间轴**均匀**取 segments 段、每段 secondsPerSegment 秒。
    ///
    /// 刻意不只看开头——老片的问题（尤其是抖动与颗粒）可能只出现在中段。
    void setSampling(int segments, double secondsPerSegment);

    /// 手动指定扫描方式（诊断误判时的逃生门）。传入 Unknown 表示恢复"仅靠检出"。
    void setScanOverride(ScanType t) { scanOverride_ = t; }
    ScanType scanOverride() const { return scanOverride_; }

    bool isRunning() const { return running_; }

    /// 异步开始诊断。`durationSec` <= 0 时按单段处理。
    /// `srcW/srcH` 由调用方提供（来自 MediaInfo），用于判断黑边是否值得裁。
    void start(const QString& input, double durationSec, int srcW, int srcH);

    /// 取消：终止当前子进程并清理临时产物。
    void cancel();

    /// 最近一次的降级/失败说明（给日志与界面）。
    QString lastNote() const { return lastNote_; }

    // ------------------------------------------------------------------
    // 以下是纯逻辑：解析与判定。不起进程、不碰文件，因此可 headless 测试。
    // 解析目标都是**实测确认过的真实输出格式**（不是按文档想象的），
    // 见各函数注释里的样例。
    // ------------------------------------------------------------------

    /// 解析 idet 统计输出。返回扫描方式，两个 ratio 走出参。
    /// 样例（stderr，多次打印，取最后一次累计值）：
    ///   [Parsed_idet_0 @ ...] Repeated Fields: Neither:   201 Top:     0 Bottom:     0
    ///   [Parsed_idet_0 @ ...] Multi frame detection: TFF:     0 BFF:   201 Progressive: 0 Undetermined: 0
    static ScanType parseIdet(const QString& text, double* interlacedRatio,
                              double* repeatedRatio);

    /// 解析 cropdetect 的 `crop=w:h:x:y`（取最后一次）。
    /// 样例：
    ///   [Parsed_cropdetect_0 @ ...] x1:0 x2:1079 y1:0 y2:1919 w:1072 h:1920 x:4 y:0 ... crop=1072:1920:4:0
    static bool parseCropdetect(const QString& text, int* w, int* h, int* x, int* y);

    /// 解析 `metadata=print` 输出的 signalstats（对多帧求均值）。
    /// 样例：
    ///   [Parsed_metadata_1 @ ...] lavfi.signalstats.YAVG=123.491
    static bool parseSignalstats(const QString& text, double* yMin, double* yMax,
                                 double* yAvg, double* satAvg);

    /// 解析 vidstabdetect 的 `.trf` 内容，返回抖动量（全局位移二阶差分标准差）。
    /// 返回 -1 表示无法判定（有效向量太少）。
    /// 样例（文本格式，每帧一行）：
    ///   Frame 2 (List 161 [(LM -24 46 540 1332 112 0.709249 3.273198),(LM 5 -1 ...),...])
    static double parseVidstabShake(const QString& trfContent);

    // ---- 分级判定（阈值集中在此，便于按实测素材标定）----
    static DiagLevel gradeDetail(double detailValue);
    static DiagLevel gradeShake(double shakeValue);
    /// 黑边是否值得裁剪：小比例差异（cropdetect 在干净画面上的正常抖动）不算黑边。
    static bool barsWorthCropping(int srcW, int srcH, int cropW, int cropH);

    // ---- 结果缓存 ----
    /// 内容指纹：**路径 + 大小 + 修改时间**。
    /// 只按路径缓存是不行的——同一路径被替换成另一个文件后结论就过期了。
    static QString fingerprint(const QString& path);
    /// 取缓存（同时校验指纹）。命中则直接可用，无需起进程。
    static bool cachedResult(const QString& path, DiagnosisResult* out);
    static void storeResult(const QString& path, const DiagnosisResult& r);
    static void clearCache();

signals:
    void finished(const vtapp::DiagnosisResult& result);
    void failed(const QString& message, const QString& detail);
    void runningChanged();

private:
    void runNextProbe();
    void startNextSegment();
    void onProcessFinished(int exitCode);
    void emitResult();
    void failWith(const QString& message, const QString& detail);
    QString tempDirPath() const;
    void cleanupTempDir();

    /// 诊断探针：一步一进程，单项失败只降级该项为「未知」，不影响其余项。
    struct Probe {
        QString id;
        QStringList args;      ///< 不含可执行文件本身；可含工作目录相对路径
        bool    needsWorkDir = false;
    };

    std::unique_ptr<QProcess> proc_;
    /// 单步超时。诊断不设超时是危险的：某些滤镜在损坏文件上会长时间不返回，
    /// 而诊断是在后台跑的，用户只会觉得"卡住了"却看不到任何原因。
    std::unique_ptr<QTimer>  stepTimer_;
    QString transcoderPath_;
    QString inputPath_;
    double  durationSec_   = 0.0;
    int     srcW_ = 0, srcH_ = 0;
    int     segments_      = 3;
    double  secondsPerSeg_ = 20.0;
    ScanType scanOverride_ = ScanType::Unknown;

    bool    running_ = false;
    bool    cancelRequested_ = false;
    /// 超时处理中：kill() 会让 finished 信号**同步**触发，
    /// 不加这个标志的话超时处理与本函数会各推进一步（诊断会"双跳"，跳过一项）。
    bool    stepTimedOut_ = false;
    QString lastNote_;

    // 探针进度
    std::vector<Probe> probes_;
    int     probeIndex_ = 0;
    int     segmentIndex_ = 0;
    QString stepStderr_;         ///< 当前探针当前段的 stderr 累积
    DiagnosisResult result_;
    QStringList shakeTrfFiles_;  ///< 各段产生的 .trf（相对临时目录）
    QString tempDir_;            ///< 需要中间产物的探针（vidstab）用
};

} // namespace vtapp
