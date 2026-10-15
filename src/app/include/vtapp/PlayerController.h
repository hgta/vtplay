#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

#include "Exporter.h"        // 导出预设与转码器信息为值类型，需完整定义
#include "FilmDiagnosis.h"   // 源诊断结论为值类型
#include "RestoreRunner.h"   // 修复导出的执行器（阶段清单 -> 一次滤镜链主遍）

// 必须在命名空间之外声明：写在 namespace vtapp 里会声明出 vtapp::QWindow 这个
// 全新类型（而非 Qt 的 ::QWindow），编译期报 "incomplete type" 且 qobject_cast 失败。
class QWindow;

namespace vtcore { class MediaPipeline; }

namespace vtapp {

/// 暴露给 QML 的播放器控制层。状态机命令 + 状态信号桥接。
class PlayerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status     READ statusString NOTIFY statusChanged)
    Q_PROPERTY(double  position   READ positionSec NOTIFY positionChanged)
    Q_PROPERTY(double  duration   READ durationSec NOTIFY durationChanged)
    Q_PROPERTY(double  rate       READ rate        WRITE setRate NOTIFY rateChanged)
    Q_PROPERTY(double  volume     READ volume      WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool    muted      READ muted       WRITE setMuted NOTIFY volumeChanged)
    Q_PROPERTY(bool    fullscreen READ fullscreen  NOTIFY fullscreenChanged)
    Q_PROPERTY(int     videoWidth  READ videoWidth  NOTIFY mediaInfoChanged)
    Q_PROPERTY(int     videoHeight READ videoHeight NOTIFY mediaInfoChanged)
    Q_PROPERTY(bool    hasVideo    READ hasVideo    NOTIFY mediaInfoChanged)
    Q_PROPERTY(bool    hasAudio    READ hasAudio    NOTIFY mediaInfoChanged)
    Q_PROPERTY(QString fileName     READ fileName     NOTIFY mediaInfoChanged)
    Q_PROPERTY(QString specSummary  READ specSummary  NOTIFY mediaInfoChanged)
    Q_PROPERTY(QString audioSummary READ audioSummary NOTIFY mediaInfoChanged)
    Q_PROPERTY(bool    hasMedia     READ hasMedia     NOTIFY mediaInfoChanged)
    Q_PROPERTY(QString windowTitle  READ windowTitle  NOTIFY mediaInfoChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)

    // ---- 应用外壳：窗口 / 视图状态 ----
    Q_PROPERTY(bool   alwaysOnTop READ alwaysOnTop WRITE setAlwaysOnTop NOTIFY alwaysOnTopChanged)
    Q_PROPERTY(QString appVersion     READ appVersion     CONSTANT)
    Q_PROPERTY(QString buildTimestamp READ buildTimestamp CONSTANT)
    Q_PROPERTY(QString qtVersion      READ qtVersion      CONSTANT)

    // 播放列表：每项 {path,name,index,current}；最近打开：{path,name,exists}
    Q_PROPERTY(QVariantList playlist     READ playlist      NOTIFY playlistChanged)
    Q_PROPERTY(int          playlistIndex READ playlistIndex NOTIFY playlistChanged)
    Q_PROPERTY(int          playlistCount READ playlistCount NOTIFY playlistChanged)
    Q_PROPERTY(QString      loopMode     READ loopMode WRITE setLoopMode NOTIFY loopModeChanged)
    Q_PROPERTY(QVariantList recentFiles  READ recentFiles   NOTIFY recentFilesChanged)
    Q_PROPERTY(bool    hasRecentFiles READ hasRecentFiles NOTIFY recentFilesChanged)
    Q_PROPERTY(QString recentIssue    READ recentIssue    NOTIFY recentIssueChanged)

    // ---- 导出（转码）----
    // 随媒体信息一起刷新：每项含 {id,label,detail,estimate}。
    // estimate 必须在 C++ 侧算好随列表下发——Q_INVOKABLE 不参与 QML 依赖追踪，
    // 用它做绑定会在启动时求值一次后永不更新（无媒体时全是 "—"）。
    Q_PROPERTY(QVariantList exportPresets   READ exportPresets   NOTIFY mediaInfoChanged)
    Q_PROPERTY(bool    ffmpegAvailable      READ ffmpegAvailable  NOTIFY transcoderChanged)
    Q_PROPERTY(QString ffmpegPath           READ ffmpegPath       NOTIFY transcoderChanged)
    Q_PROPERTY(bool    exportRunning        READ exportRunning    NOTIFY exportStateChanged)
    Q_PROPERTY(double  exportProgress       READ exportProgress   NOTIFY exportProgressChanged)

    // ---- 源诊断（老片修复的前置；诊断本身也有独立价值）----
    //
    // 沿用本项目既有做法：在 C++ 侧就把**可直接显示的字符串**算好再下发
    // （参考 specSummary / exportPresets 的注释：Q_INVOKABLE 不参与 QML 依赖追踪，
    //   会破坏绑定刷新）。分级与扫描方式在这里就翻成「轻度」「交错（上场优先）」。
    Q_PROPERTY(QVariantMap sourceDiagnosis  READ sourceDiagnosis  NOTIFY diagnosisChanged)
    Q_PROPERTY(bool        diagnosisRunning READ diagnosisRunning NOTIFY diagnosisChanged)

        /// 修复意图（可读写）。字段见 RestoreIntent。
        ///
        /// 刻意用整份 map 读写、而不是逐个 Q_PROPERTY：意图字段会随能力增长而增加，
        /// 逐个暴露会让"加一个开关"变成三处同步改动（头文件、实现、QML 绑定）。
        /// 更关键的是——两端各存一份状态必然漂移，这里只有一个来源。
        Q_PROPERTY(QVariantMap restoreIntent  READ restoreIntent  WRITE setRestoreIntent
                               NOTIFY restoreIntentChanged)
        /// 由「诊断结论 + 当前意图」推导出的**将要执行什么**。
        ///
        /// 界面只呈现它、不做任何推导：决策只有一处（纯函数 buildPlan，可穷举断言）。
        /// 用户先看到"它打算做什么"，才谈得上"要不要让它动我的画面"。
        Q_PROPERTY(QVariantMap restorePreview READ restorePreview NOTIFY restoreIntentChanged)

    /// 预览图的 URL（file://…），未渲染过为空串。
    Q_PROPERTY(QString restorePreviewImage READ restorePreviewImage NOTIFY restorePreviewChanged)
    /// 预览失败原因，成功为空串。
    /// 与"尚未渲染"分开表达：混成一个空串就分不清"还没点过"和"点了但失败"。
    Q_PROPERTY(QString restorePreviewError READ restorePreviewError NOTIFY restorePreviewChanged)

    /// 当前修复阶段名，非修复路径为空串。
    ///
    /// 必须做成 Q_PROPERTY 而不是只留一个 C++ 方法：Q_INVOKABLE/普通方法不参与
    /// QML 的属性依赖追踪，界面读它时不会随进度变化刷新（会一直显示旧阶段）。
    /// 通知复用 exportProgressChanged，与 exportSpeed / exportWrittenText 一致。
    Q_PROPERTY(QString exportStageText READ exportStageText NOTIFY exportProgressChanged)
    Q_PROPERTY(double  exportSpeed          READ exportSpeed      NOTIFY exportProgressChanged)
    Q_PROPERTY(QString exportWrittenText    READ exportWrittenText NOTIFY exportProgressChanged)
    Q_PROPERTY(QString exportOutputPath     READ exportOutputPath NOTIFY exportOutputPathChanged)

    // ---- 设置页（随转码器探测 / 截图目录变化刷新）----
    Q_PROPERTY(QString ffmpegConfiguredPath READ ffmpegConfiguredPath NOTIFY transcoderChanged)
    Q_PROPERTY(QString ffmpegSourceLabel    READ ffmpegSourceLabel    NOTIFY transcoderChanged)
    Q_PROPERTY(QString screenshotDirectory  READ screenshotDirectory  NOTIFY screenshotDirectoryChanged)
    Q_PROPERTY(QString screenshotContent    READ screenshotContent    NOTIFY screenshotContentChanged)

public:
    explicit PlayerController(QObject* parent = nullptr);
    ~PlayerController();

    QString statusString() const;
    double  positionSec() const;
    double  durationSec() const;
    double  rate() const;
    void    setRate(double r);
    double  volume() const;
    void    setVolume(double v);
    bool    muted() const { return muted_; }
    void    setMuted(bool m);
    bool    fullscreen() const;
    int     videoWidth() const;
    int     videoHeight() const;
    bool    hasVideo() const;
    bool    hasAudio() const;
    QString errorString() const { return errorString_; }

    // ---- 当前媒体元数据（供界面显示与导出使用）----
    /// 文件名（不含目录）；未加载媒体时为空串。
    QString fileName() const;
    /// 规格摘要，如 "2160×3840 · 60fps · 54Mbps · 158MB"；字段缺失自动省略。
    QString specSummary() const;
    /// 音频摘要，如 "48kHz · 立体声"；字段缺失自动省略。
    QString audioSummary() const;
    /// 是否已加载媒体（有视频或音频）。
    bool    hasMedia() const;
    /// 窗口标题：`VTPlay — <文件名>`；未加载媒体时为 `VTPlay`。
    QString windowTitle() const;

    // ---- 导出：状态读取 ----
    QVariantList exportPresets() const;       // [{id,label,detail}, ...]
    bool    ffmpegAvailable() const { return transcoder_.available; }
    QString ffmpegPath() const      { return transcoder_.path; }
    bool    exportRunning() const;
    double  exportProgress() const  { return exportProgress_; }   // 0..1，未知为 -1
    double  exportSpeed() const     { return exportSpeed_; }
    QString exportWrittenText() const;
    QString exportOutputPath() const { return exportOutputPath_; }

    /// 指定预设下的预估输出体积（可读文本，如 "7.6 MB"）。
    /// 注意：这是**上界**（按目标码率计算），CRF 编码实际通常更小。
    Q_INVOKABLE QString estimateForPreset(const QString& presetId) const;

    /// 指定预设下的默认输出路径（与源同目录、自动避开重名）。
    Q_INVOKABLE QString defaultOutputPath(const QString& presetId) const;

    /// 重新探测转码器（用户配置路径后调用）。
    Q_INVOKABLE void probeTranscoder();

    // ---- 应用外壳：窗口 / 视图 ----
    bool    alwaysOnTop() const { return alwaysOnTop_; }
    void    setAlwaysOnTop(bool v);

    QString appVersion() const;
    QString buildTimestamp() const;
    QString qtVersion() const;

    /// 「关于」页所需：转码器版本（懒探测并缓存，首次调用会阻塞几百毫秒）。
    Q_INVOKABLE QString ffmpegVersionText() const;
    /// 让上面的版本缓存失效（换转码器路径后必须调用）。
    void clearTranscoderVersionCache();
    /// 当前默认音频输出设备名；无设备时返回“未找到音频输出设备”。
    Q_INVOKABLE QString audioDeviceName() const;

    /// 不影响播放的刷新节流：窗口不可见/最小化时降低进度刷新频率（省电）。
    Q_INVOKABLE void setWindowActive(bool active);

    // ---- 设置页 ----
    /// 用户手动指定的转码器路径（空串 = 未指定，走自动探测）。
    QString ffmpegConfiguredPath() const { return configuredFfmpeg_; }
    /// 当前转码器是怎么找到的（中文短语，直接可显示）。
    QString ffmpegSourceLabel() const;
    /// 截图保存目录；未指定时为系统图片目录。
    QString screenshotDirectory() const;

    /// 手动指定转码器路径；传空串等同于恢复自动探测。会立刻重新探测并落盘。
    Q_INVOKABLE void setFfmpegPath(const QString& path);
    /// 设置截图目录（不存在时尝试创建；传空串恢复系统图片目录）。
    Q_INVOKABLE void setScreenshotDirectory(const QString& dir);
    /// 系统图片目录（「恢复默认」用）。
    Q_INVOKABLE QString defaultScreenshotDirectory() const;

    // ---- 状态持久化 ----
    /// QML 里把主窗口交给控制器：几何校验需要外框尺寸（见 windowGeometry 注释）。
    Q_INVOKABLE void attachWindow(QObject* window);

    /// 恢复的窗口几何（**客户区**坐标，与 QML `Window.x/y` 语义一致）；
    /// `valid` 为 false 时调用方应使用默认尺寸。
    ///
    /// 校验时用「外框矩形」而不是客户区：QML 的 x/y 是客户区坐标，标题栏与边框
    /// 在它之上/之左，只按客户区判断会把标题栏推到屏幕外（实测 y=0 时标题栏
    /// 整条位于屏幕之上，窗口无法拖动）。
    Q_INVOKABLE QVariantMap windowGeometry() const;
    /// 保存窗口正常态几何（全屏时不调用，避免把全屏尺寸存成常态）。
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height,
                                        bool maximized);
    /// 退出前落盘（音量/静音等低频状态）。
    Q_INVOKABLE void persistSettings();

    /// 诊断结论（已格式化为可直接显示的短语，QML 侧不需要再做翻译）
    QVariantMap sourceDiagnosis() const;
    bool        diagnosisRunning() const;

    /// 开始源诊断（后台执行、不阻塞播放）。
    /// 诊断需要起十余个子进程、耗时数十秒，因此**按需触发**而不是打开文件就自动跑。
    Q_INVOKABLE void runDiagnosis();
    Q_INVOKABLE void cancelDiagnosis();

    // ---- 修复导出 ----

    QVariantMap restoreIntent() const;
    void        setRestoreIntent(const QVariantMap& intent);
    QVariantMap restorePreview() const;

    /// 按当前修复意图导出。
    ///
    /// `plan.valid` 为 false 时**拒绝执行并报错**，不"尽力而为"——顺序约束被破坏的
    /// 组合会产生永久性伪影（如梳状纹被放大后加粗，之后再也去不掉），
    /// 宁可不出结果，也不出一个错的结果。
    Q_INVOKABLE void startRestoreExport(const QString& outputPath);

    /// 当前修复阶段的名称（如「降噪」），非修复路径为空串。
    ///
    /// 修复是分阶段串行的，各阶段耗时差异可达十倍以上；只给一个总百分比时，
    /// 用户无法区分"卡住了"和"正在跑慢阶段"。
    QString exportStageText() const;

    QString restorePreviewImage() const;
    QString restorePreviewError() const;

    /// 按当前方案把 `atSec` 处的一帧渲染成 PNG，返回空串表示成功。
    ///
    /// **同步**执行（实测 < 1 秒）：这样能在按钮点击里直接拿到结果，
    /// 不必为"预览"再建一套异步状态机。
    ///
    /// 渲染用的是 `previewPlan()`——它**跳过稳定阶段**（稳定要"两遍全片处理 +
    /// 中间文件"，单帧无法执行）。因此预览不含稳定效果，界面必须如实标注，
    /// 否则用户会以为稳定没生效。半成品预览 + 明确说明，优于为了"完整"而不可用。
    Q_INVOKABLE QString renderRestorePreview(double atSec);

    // ---- 播放列表（本次会话的队列）----
    QVariantList playlist() const;
    int          playlistIndex() const { return playlistIndex_; }
    int          playlistCount() const { return playlist_.size(); }
    QString      loopMode() const { return loopMode_; }
    void         setLoopMode(const QString& mode);

    /// 追加到队列；`urls` 为空则无操作。若当前没有媒体，自动播放第一项。
    Q_INVOKABLE void addToPlaylist(const QList<QUrl>& urls);
    Q_INVOKABLE void playlistPlayAt(int index);
    Q_INVOKABLE void playlistRemoveAt(int index);
    Q_INVOKABLE void playlistClear();
    Q_INVOKABLE void playlistNext();
    Q_INVOKABLE void playlistPrevious();

    // ---- 最近打开（历史，只读 + 清空）----
    QVariantList recentFiles() const;
    bool         hasRecentFiles() const { return !recent_.isEmpty(); }
    QString      recentIssue() const { return recentIssue_; }
    Q_INVOKABLE void clearRecentFiles();

    // ---- 实用功能 ----
    /// 截图的默认保存路径（截图目录，`<basename>_<时间戳>.png`）。
    Q_INVOKABLE QString screenshotFilePath() const;

    /// 截图内容：`frame`（默认，纯画面）或 `window`（含界面控制条）。
    QString screenshotContent() const { return screenshotContent_; }
    Q_INVOKABLE void setScreenshotContent(const QString& mode);

    /// 把当前显示帧存成 PNG（纯画面路径）。无帧可截时返回 false。
    Q_INVOKABLE bool saveCurrentFrame(const QString& path) const;
    /// 当前媒体的本地绝对路径（供「在文件夹中显示」）；未加载时为空串。
    Q_INVOKABLE QString sourcePath() const;
    /// 用系统文件管理器打开该文件所在目录并选中它。
    Q_INVOKABLE void openContainingFolder(const QString& filePath);

public slots:
    void openUrl(const QUrl& url);
    void open(const QString& path);
    void play();
    void pause();
    void togglePlay();
    void stop();
    void seek(double sec);
    void seekDelta(double sec);
    /// 逐帧步进：direction > 0 前进一帧，< 0 后退一帧。
    /// 播放中调用会先暂停——逐帧是「检视」动作，边播边步进没有意义。
    void stepFrame(int direction);
    void toggleFullscreen();

    /// QML 里 VideoRenderer 实例创建后调用，把渲染项挂接到管线。
    void attachRenderer(QObject* renderer);

    /// QML 传入的最新一帧；VideoRenderer 负责从管线拉取并显示。
    QObject* renderer() const;
    QObject* audio() const;

    /// 自定义预设下的预估体积（随用户选择的参数变化）。
    Q_INVOKABLE QString estimateCustom(int shortSide, int videoKbps) const;

    // ---- 导出：命令 ----
    /// 开始导出。outputPath 为空时使用默认路径。
    /// presetId == "custom" 时用 customShortSide / customVideoKbps 覆盖预设参数。
    Q_INVOKABLE void startExport(const QString& presetId, const QString& outputPath,
                                 int customShortSide = 0, int customVideoKbps = 0);
    Q_INVOKABLE void cancelExport();
    /// 用系统文件管理器打开输出所在目录（导出成功后可用）。
    Q_INVOKABLE void openOutputFolder();

    /// 把 QML FileDialog 返回的 URL 转成本地路径。
    /// 在 QML 里手工剥离 `file:///` 前缀在 Windows 上极易出错，统一走这里。
    Q_INVOKABLE QString localPathFromUrl(const QUrl& url) const { return url.toLocalFile(); }

signals:
    void statusChanged();
    void positionChanged();
    void durationChanged();
    void rateChanged();
    void volumeChanged();
    void fullscreenChanged();
    void mediaInfoChanged();
    void errorChanged();
    void requestOpenDialog();

    // ---- 导出 ----
    void transcoderChanged();
    void exportStateChanged();
    void exportProgressChanged();
    void exportOutputPathChanged();
    void exportFinished(const QString& outputPath);
    void exportFailed(const QString& message, const QString& detail);
    void exportCancelled();

    // ---- 应用外壳 ----
    void alwaysOnTopChanged();
    void playlistChanged();
    void loopModeChanged();
    void recentFilesChanged();
    void recentIssueChanged();
    void screenshotDirectoryChanged();
    void screenshotContentChanged();
    /// 用户把文件拖入窗口 / 侧栏请求打开文件对话框
    void requestAddFiles();

    /// 源诊断结论变化（一个信号覆盖上面两个属性）
    void diagnosisChanged();
    /// 意图变化，或诊断结论变化导致推导结果变化时发出。
    void restoreIntentChanged();
    void restorePreviewChanged();

private:
    std::unique_ptr<vtcore::MediaPipeline> pipeline_;
    class VideoRenderer*    renderer_ = nullptr;
    class AudioOutput*       audio_ = nullptr;
    bool    muted_ = false;
    double  prevVolume_ = 1.0;
    bool    fullscreen_ = false;
    QString errorString_;

    // ---- 导出状态 ----
    std::unique_ptr<Exporter> exporter_;
    std::unique_ptr<FilmDiagnosis> filmDiag_;   ///< 源诊断（惰性创建）
    DiagnosisResult diag_;                      ///< 最近一次诊断结论
    RestoreIntent   intent_;                    ///< 修复意图（界面可调，默认不处理）
    /// 修复执行器。与 exporter_ 一样随控制器创建：它在 start() 之前不起任何子进程，
    /// 提前创建只是为了避免"首次使用时才接线"这种容易漏掉的路径。
    std::unique_ptr<RestoreRunner> restoreRunner_;
    /// 当前阶段（修复路径）；非修复路径保持为空/-1。
    QString exportStageName_;
    int     exportStageIndex_ = -1;

    // ---- 修复预览 ----
    QString restorePreviewImage_;   ///< 预览图 URL
    QString restorePreviewError_;
    /// 递增序号拼进 URL。QML 的 Image 在 source 不变时不会重新加载，
    /// 于是"连续预览同一秒"会一直显示旧图——看起来像预览没反应。
    int     restorePreviewSeq_ = 0;
    TranscoderInfo transcoder_;
    double  exportProgress_ = -1.0;
    double  exportSpeed_    = 0.0;
    qint64  exportWritten_  = 0;
    QString exportOutputPath_;

    // ---- 进度刷新（事件驱动，替代 QML 侧 100ms 轮询）----
    /// 由定时器推进：读一次原子位置并 emit，节流集中在这里便于统一调整。
    void tick();
    /// 状态跃迁时立刻补发一次位置/时长，暂停、seek、打开都要即时反映。
    void emitPositionNow();

    /// 转码器版本缓存（按路径生效）
    mutable bool    ffmpegVersionProbed_ = false;
    mutable QString ffmpegVersionPath_;
    mutable QString ffmpegVersionCache_;

    QWindow* window_ = nullptr;   ///< 主窗口，仅用于几何校验（不持有所有权）
    QTimer  positionTimer_;
    int     lastStatus_ = -1;      ///< 上一拍的状态，用于识别 Eof 等跃迁
    bool    windowActive_ = true;  ///< 窗口不可见时降频

    // ---- 应用外壳状态 ----
    bool         alwaysOnTop_ = false;
    QString      configuredFfmpeg_;   ///< 用户指定的 ffmpeg 路径（空 = 自动探测）
    QString      screenshotDir_;      ///< 用户指定的截图目录（空 = 系统图片目录）
    QString      screenshotContent_ = QStringLiteral("frame");  ///< frame | window
    QStringList  playlist_;
    int          playlistIndex_ = -1;
    QString      loopMode_ = QStringLiteral("none");   ///< none | one | all
    QStringList  recent_;                              ///< 最近打开（最新在前）
    QString      recentIssue_;                         ///< 失效条目提示（如“已移除 2 个失效项”）

    void pushRecent(const QString& path);
    void handleEndOfFile();
};

} // namespace vtapp