#include "PlayerController.h"
#include "VideoRenderer.h"
#include "AudioOutput.h"

#include "MediaPipeline.h"
#include "MediaInfo.h"
#include "PlayerState.h"

#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QWindow>
#include <QVariantMap>

namespace {
// 进度刷新节流：30ms 足够平滑（进度条本身还有动画），又远低于每帧开销
constexpr int kPositionIntervalMs       = 30;
// 窗口不可见/最小化时降频（省电；此时用户看不到进度变化）
constexpr int kPositionIntervalIdleMs   = 500;
constexpr int kMaxRecentFiles           = 10;
constexpr char kSettingsRecent[]        = "history/recentFiles";
} // namespace

namespace vtapp {

PlayerController::PlayerController(QObject* parent) : QObject(parent) {
    pipeline_ = std::make_unique<vtcore::MediaPipeline>();
    audio_    = new AudioOutput(this);
    audio_->attach(pipeline_.get());
    // renderer 由 QML 创建，attachRenderer() 时挂接。

    // ---- 导出器：与播放管线完全独立（外部进程） ----
    exporter_ = std::make_unique<Exporter>();

    connect(exporter_.get(), &Exporter::progressChanged, this,
            [this](double ratio, double speed, qint64 written) {
        exportProgress_ = ratio;
        exportSpeed_    = speed;
        exportWritten_  = written;
        emit exportProgressChanged();
    });
    connect(exporter_.get(), &Exporter::runningChanged, this, [this]() {
        emit exportStateChanged();
    });
    connect(exporter_.get(), &Exporter::finished, this, [this](const QString& path) {
        exportProgress_   = 1.0;
        exportOutputPath_ = path;
        emit exportProgressChanged();
        emit exportOutputPathChanged();
        emit exportFinished(path);
    });
    connect(exporter_.get(), &Exporter::failed, this,
            [this](const QString& message, const QString& detail) {
        exportProgress_ = -1.0;
        emit exportProgressChanged();
        emit exportFailed(message, detail);
    });
    connect(exporter_.get(), &Exporter::cancelled, this, [this]() {
        exportProgress_ = -1.0;
        emit exportProgressChanged();
        emit exportCancelled();
    });

    // 启动时探测一次转码器：导出入口据此决定是否可用（约几十毫秒）。
    // 必须先读用户配置的路径——探测顺序的第一级就是它，读晚了首次探测会忽略配置。
    configuredFfmpeg_ = QSettings().value(QStringLiteral("transcoder/path"))
                                     .toString().trimmed();
    probeTranscoder();

    // ---- 进度刷新：事件驱动（替代 QML 侧每 100ms 手动 emit 的反模式）----
    positionTimer_.setInterval(kPositionIntervalMs);
    positionTimer_.setTimerType(Qt::PreciseTimer);
    connect(&positionTimer_, &QTimer::timeout, this, &PlayerController::tick);

    // ---- 恢复持久化状态（音量/静音/置顶/循环模式/最近打开）----
    // 设置读写失败不阻止启动：QSettings 读不到就取默认值。
    QSettings s;
    setVolume(s.value(QStringLiteral("audio/volume"), 1.0).toDouble());
    const bool m = s.value(QStringLiteral("audio/muted"), false).toBool();
    if (m) { muted_ = true; if (audio_) audio_->setMuted(true); }
    alwaysOnTop_ = s.value(QStringLiteral("window/alwaysOnTop"), false).toBool();
    const QString lm = s.value(QStringLiteral("playback/loopMode"),
                               QStringLiteral("none")).toString();
    if (lm == QLatin1String("one") || lm == QLatin1String("all")) loopMode_ = lm;

    screenshotDir_ = s.value(QStringLiteral("screenshot/directory")).toString().trimmed();
    {
        // 默认「仅画面」：绝大多数场合用户要的是那一帧，而不是连控制条一起截进去
        const QString sc = s.value(QStringLiteral("screenshot/content"),
                                   QStringLiteral("frame")).toString();
        if (sc == QLatin1String("window")) screenshotContent_ = sc;
    }

    recent_ = s.value(QLatin1String(kSettingsRecent)).toStringList();
    // 失效条目（文件被移动/删除）在启动时就剔除，并给出提示
    {
        QStringList alive;
        int dropped = 0;
        for (const auto& p : recent_) {
            if (QFileInfo::exists(p)) alive << p; else ++dropped;
        }
        if (dropped > 0) {
            recent_ = alive;
            recentIssue_ = QStringLiteral("已从最近打开中移除 %1 个失效项").arg(dropped);
            s.setValue(QLatin1String(kSettingsRecent), recent_);
        }
    }

    // 生效中的设置落一条日志：设置页改了路径却「看不出区别」时，
    // 这行能立刻说明到底用了哪个转码器、截图会写到哪。
    qInfo("[settings] transcoder available=%s source=%s",
          transcoder_.available ? "yes" : "no", qPrintable(transcoder_.source));
    qInfo("[settings] transcoder path=%s", qPrintable(transcoder_.path));
    qInfo("[settings] screenshot dir=%s",  qPrintable(screenshotDirectory()));
}

PlayerController::~PlayerController() = default;

QString PlayerController::statusString() const {
    return QString::fromUtf8(vtcore::toString(pipeline_->status()));
}

double PlayerController::positionSec() const { return pipeline_->positionSec(); }
double PlayerController::durationSec() const { return pipeline_->durationSec(); }
double PlayerController::rate() const         { return pipeline_->rate(); }

void PlayerController::setRate(double r) {
    if (qFuzzyCompare(pipeline_->rate(), r)) return;
    pipeline_->setRate(r);
    emit rateChanged();
}

double PlayerController::volume() const { return pipeline_->volume(); }

void PlayerController::setVolume(double v) {
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    if (qFuzzyCompare(pipeline_->volume(), v)) return;
    pipeline_->setVolume(v);
    if (audio_) audio_->setVolume(v);
    if (v > 0) muted_ = false;
    emit volumeChanged();
}

void PlayerController::setMuted(bool m) {
    if (muted_ == m) return;
    muted_ = m;
    if (audio_) audio_->setMuted(m);
    emit volumeChanged();
}

bool PlayerController::fullscreen() const { return fullscreen_; }

int PlayerController::videoWidth()  const { return pipeline_->info().videoWidth; }
int PlayerController::videoHeight() const { return pipeline_->info().videoHeight; }
bool PlayerController::hasVideo()   const { return pipeline_->info().hasVideo; }
bool PlayerController::hasAudio()   const { return pipeline_->info().hasAudio; }

QString PlayerController::fileName() const {
    return QString::fromStdString(pipeline_->info().fileName);
}

QString PlayerController::specSummary() const {
    return QString::fromStdString(vtcore::formatSpecSummary(pipeline_->info()));
}

QString PlayerController::audioSummary() const {
    const auto in = pipeline_->info();
    QStringList parts;
    if (in.audioSampleRate > 0) {
        const double khz = in.audioSampleRate / 1000.0;
        const bool integral = (in.audioSampleRate % 1000) == 0;
        parts << QStringLiteral("%1kHz").arg(khz, 0, 'f', integral ? 0 : 1);
    }
    switch (in.audioChannels) {
        case 0:  break;                                                        // 不可用
        case 1:  parts << QStringLiteral("单声道"); break;
        case 2:  parts << QStringLiteral("立体声"); break;
        default: parts << QStringLiteral("%1 声道").arg(in.audioChannels); break;
    }
    return parts.join(QStringLiteral(" · "));
}

bool PlayerController::hasMedia() const {
    return pipeline_->info().hasVideo || pipeline_->info().hasAudio;
}

QString PlayerController::windowTitle() const {
    const QString name = fileName();
    return name.isEmpty() ? QStringLiteral("VTPlay")
                          : QStringLiteral("VTPlay — %1").arg(name);
}

void PlayerController::openUrl(const QUrl& url) {
    open(url.toLocalFile().isEmpty() ? url.toString() : url.toLocalFile());
}

void PlayerController::open(const QString& path) {
    if (path.isEmpty()) return;
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        errorString_ = QStringLiteral("文件不存在：%1").arg(path);
        emit errorChanged();
        return;
    }
    errorString_.clear();
    try {
        const QString abs = fi.absoluteFilePath();
        pipeline_->open(abs.toStdString());
        pushRecent(abs);
        // 若该文件已在播放列表中，同步当前项（侧栏高亮才不会错位）
        const int idx = playlist_.indexOf(abs);
        if (idx >= 0 && idx != playlistIndex_) {
            playlistIndex_ = idx;
            emit playlistChanged();
        }
        emit statusChanged();
        emit durationChanged();
        emit mediaInfoChanged();
        emit positionChanged();
        // 打开成功后立即开始播放：符合播放器惯例。
        // 否则界面停在首帧（看起来"画面正常"），用户会以为"没声音/没反应"。
        this->play();
    } catch (const std::exception& e) {
        errorString_ = QString::fromUtf8(e.what());
        emit errorChanged();
    }
}

void PlayerController::play() {
    pipeline_->play();
    lastStatus_ = static_cast<int>(pipeline_->status());
    positionTimer_.start();
    emit statusChanged();
}
void PlayerController::pause() {
    pipeline_->pause();
    positionTimer_.stop();
    // 暂停后必须立刻补发一次：否则进度条会停在 30ms 前的旧值
    emitPositionNow();
    emit statusChanged();
}
void PlayerController::togglePlay() {
    auto s = pipeline_->status();
    if (s == vtcore::PlayerStatus::Playing) pause();
    else if (s == vtcore::PlayerStatus::Paused ||
             s == vtcore::PlayerStatus::Eof    ||
             s == vtcore::PlayerStatus::Loading) play();
}

void PlayerController::stop() {
    pipeline_->stop();
    positionTimer_.stop();
    lastStatus_ = static_cast<int>(pipeline_->status());
    emit statusChanged();
    emit durationChanged();
    emit positionChanged();
    // 媒体已卸载：通知界面清空文件名与规格（避免残留上一个文件的信息）。
    emit mediaInfoChanged();
}

void PlayerController::seek(double sec) {
    pipeline_->seek(sec);
    emit positionChanged();
}

void PlayerController::seekDelta(double sec) {
    double target = pipeline_->positionSec() + sec;
    if (target < 0) target = 0;
    pipeline_->seek(target);
    emit positionChanged();
}

void PlayerController::emitPositionNow() {
    emit positionChanged();
    emit durationChanged();
}

void PlayerController::tick() {
    // 状态跃迁（播->停、进入 Eof 等）在这里统一识别：一次原子读，代价极低。
    const int st = static_cast<int>(pipeline_->status());
    if (st != lastStatus_) {
        const int prev = lastStatus_;
        lastStatus_ = st;
        emit statusChanged();
        if (prev != static_cast<int>(vtcore::PlayerStatus::Eof) &&
            st   == static_cast<int>(vtcore::PlayerStatus::Eof)) {
            handleEndOfFile();
        }
        if (st != static_cast<int>(vtcore::PlayerStatus::Playing)) {
            // 已不在播放：停止高频刷新，但保留定时器以便继续观察状态跃迁
            positionTimer_.setInterval(kPositionIntervalIdleMs);
        } else {
            positionTimer_.setInterval(windowActive_ ? kPositionIntervalMs
                                                     : kPositionIntervalIdleMs);
        }
    }
    emit positionChanged();
}

void PlayerController::setWindowActive(bool active) {
    if (windowActive_ == active) return;
    windowActive_ = active;
    positionTimer_.setInterval(active ? kPositionIntervalMs
                                      : kPositionIntervalIdleMs);
}

void PlayerController::handleEndOfFile() {
    // 循环模式影响播放结束行为。默认 "none" 保持原有表现（停在结束状态显示尾帧）。
    if (loopMode_ == QLatin1String("one")) {
        pipeline_->seek(0.0);
        pipeline_->play();
        lastStatus_ = static_cast<int>(pipeline_->status());
    } else if (loopMode_ == QLatin1String("all") && playlist_.size() > 1) {
        playlistNext();
    }
    emit positionChanged();
}

void PlayerController::stepFrame(int direction) {
    if (!pipeline_ || !pipeline_->info().hasVideo) return;
    // 播放中先暂停：逐帧是「检视」动作，边播边步进帧会一闪而过
    if (pipeline_->status() == vtcore::PlayerStatus::Playing) pipeline_->pause();
    if (!pipeline_->stepFrame(direction)) return;
    // 立即反映：不等下一拍，否则连续步进时进度与画面会滞后
    emitPositionNow();
    emit statusChanged();
}

void PlayerController::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    emit fullscreenChanged();
}

QObject* PlayerController::renderer() const { return renderer_; }
QObject* PlayerController::audio() const    { return audio_; }

void PlayerController::attachRenderer(QObject* r) {
    auto* vr = qobject_cast<VideoRenderer*>(r);
    if (!vr || vr == renderer_) return;
    renderer_ = vr;
    renderer_->attach(pipeline_.get());
}

// ---------------------------------------------------------------- 导出

QVariantList PlayerController::exportPresets() const {
    QVariantList out;
    const auto info = pipeline_->info();
    const auto list = Exporter::builtinPresets();
    for (const auto& p : list) {
        QVariantMap m;
        m.insert(QStringLiteral("id"),     p.id);
        m.insert(QStringLiteral("label"),  p.label);
        m.insert(QStringLiteral("detail"), p.detail);
        const qint64 est = Exporter::estimateBytes(p, info.durationSec,
                                                   info.fileSizeBytes);
        m.insert(QStringLiteral("estimate"),
                 est > 0 ? QString::fromStdString(vtcore::formatSize(est))
                         : QStringLiteral("—"));
        out.append(m);
    }
    return out;
}

bool PlayerController::exportRunning() const {
    return exporter_ && exporter_->isRunning();
}

QString PlayerController::exportWrittenText() const {
    if (exportWritten_ <= 0) return QString();
    return QString::fromStdString(vtcore::formatSize(exportWritten_));
}

QString PlayerController::estimateForPreset(const QString& presetId) const {
    const auto info = pipeline_->info();
    const auto preset = Exporter::presetById(presetId);
    const qint64 est = Exporter::estimateBytes(preset, info.durationSec,
                                               info.fileSizeBytes);
    if (est <= 0) return QStringLiteral("—");
    return QString::fromStdString(vtcore::formatSize(est));
}

QString PlayerController::defaultOutputPath(const QString& presetId) const {
    const QString src = QString::fromStdString(pipeline_->info().url);
    if (src.isEmpty()) return QString();
    return Exporter::makeOutputPath(src, Exporter::presetById(presetId).suffix);
}

void PlayerController::probeTranscoder() {
    // 带上用户配置的路径：探测顺序是「配置 → 应用同级目录 → PATH」
    transcoder_ = Exporter::probeTranscoder(configuredFfmpeg_);
    if (exporter_) exporter_->setTranscoderPath(transcoder_.path);
    // 版本随路径变化，必须让「关于」页的缓存失效
    clearTranscoderVersionCache();
    emit transcoderChanged();
}

QString PlayerController::ffmpegSourceLabel() const {
    if (!transcoder_.available) return QStringLiteral("未找到");
    if (transcoder_.source == QLatin1String("configured")) return QStringLiteral("手动指定");
    if (transcoder_.source == QLatin1String("appdir"))     return QStringLiteral("应用目录");
    if (transcoder_.source == QLatin1String("PATH"))       return QStringLiteral("系统 PATH");
    return transcoder_.source;
}

void PlayerController::setFfmpegPath(const QString& path) {
    const QString clean = path.trimmed();
    if (clean == configuredFfmpeg_) return;
    configuredFfmpeg_ = clean;
    QSettings().setValue(QStringLiteral("transcoder/path"), clean);
    probeTranscoder();
}

QString PlayerController::defaultScreenshotDirectory() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return dir.isEmpty() ? QDir::homePath() : dir;
}

QString PlayerController::screenshotDirectory() const {
    return screenshotDir_.isEmpty() ? defaultScreenshotDirectory() : screenshotDir_;
}

void PlayerController::setScreenshotContent(const QString& mode) {
    const QString clean = (mode == QLatin1String("window")) ? mode : QStringLiteral("frame");
    if (clean == screenshotContent_) return;
    screenshotContent_ = clean;
    QSettings().setValue(QStringLiteral("screenshot/content"), clean);
    emit screenshotContentChanged();
}

bool PlayerController::saveCurrentFrame(const QString& path) const {
    if (!renderer_) return false;
    const QImage frame = renderer_->currentFrameImage();
    if (frame.isNull()) return false;
    // 目录不存在时补建（与 screenshotFilePath() 同样的兜底）
    const QString dir = QFileInfo(path).absolutePath();
    if (!QFileInfo(dir).isDir()) QDir().mkpath(dir);
    return frame.save(path, "PNG");
}

void PlayerController::setScreenshotDirectory(const QString& dir) {
    const QString clean = dir.trimmed();
    // 与默认目录相同就按「未指定」存：这样用户改了系统图片目录后能自动跟随
    const QString store = (clean.isEmpty() || clean == defaultScreenshotDirectory())
                          ? QString() : clean;
    if (store == screenshotDir_) return;
    screenshotDir_ = store;
    QSettings().setValue(QStringLiteral("screenshot/directory"), store);
    emit screenshotDirectoryChanged();
}

QString PlayerController::estimateCustom(int shortSide, int videoKbps) const {
    ExportPreset p = Exporter::presetById(QStringLiteral("custom"));
    if (shortSide  > 0) p.shortSide = shortSide;
    if (videoKbps  > 0) p.videoKbps = videoKbps;
    const auto info = pipeline_->info();
    const qint64 est = Exporter::estimateBytes(p, info.durationSec, info.fileSizeBytes);
    if (est <= 0) return QStringLiteral("—");
    return QString::fromStdString(vtcore::formatSize(est));
}

void PlayerController::startExport(const QString& presetId, const QString& outputPath,
                                   int customShortSide, int customVideoKbps) {
    if (!exporter_ || exporter_->isRunning()) return;

    const auto info = pipeline_->info();
    if (!info.hasVideo && !info.hasAudio) {
        emit exportFailed(QStringLiteral("没有可导出的媒体"),
                          QStringLiteral("请先打开一个视频文件。"));
        return;
    }

    ExportPreset preset = Exporter::presetById(presetId);
    if (presetId == QLatin1String("custom")) {
        if (customShortSide > 0) preset.shortSide = customShortSide;
        if (customVideoKbps > 0) preset.videoKbps = customVideoKbps;
    }
    const QString out = outputPath.isEmpty()
        ? Exporter::makeOutputPath(QString::fromStdString(info.url), preset.suffix)
        : outputPath;

    exportProgress_   = 0.0;
    exportSpeed_      = 0.0;
    exportWritten_    = 0;
    exportOutputPath_ = out;
    emit exportProgressChanged();
    emit exportOutputPathChanged();

    const qint64 est = Exporter::estimateBytes(preset, info.durationSec,
                                               info.fileSizeBytes);
    exporter_->start(preset, QString::fromStdString(info.url), out,
                     info.videoWidth, info.videoHeight, info.hasAudio,
                     info.durationSec, est);

    // 启动可能**同步**失败（空间不足 / 无转码器），此时不会走 runningChanged
    emit exportStateChanged();
}

void PlayerController::cancelExport() {
    if (exporter_) exporter_->cancel();
}

void PlayerController::openOutputFolder() {
    if (exportOutputPath_.isEmpty()) return;
    openContainingFolder(exportOutputPath_);
}

// ------------------------------------------------- 应用外壳：窗口 / 视图

QString PlayerController::appVersion() const {
#ifdef VTPLAY_VERSION
    return QStringLiteral(VTPLAY_VERSION);
#else
    return QStringLiteral("dev");
#endif
}

QString PlayerController::buildTimestamp() const {
    return QStringLiteral("%1 %2").arg(QStringLiteral(__DATE__), QStringLiteral(__TIME__));
}

QString PlayerController::qtVersion() const { return QString::fromLatin1(qVersion()); }

QString PlayerController::ffmpegVersionText() const {
    // 缓存按路径做键：换了转码器必须重新探测（曾在设置页改了路径但「关于」页
    // 仍显示旧版本，因为老实现用的是与路径无关的 static 缓存）。
    if (ffmpegVersionProbed_ && ffmpegVersionPath_ == transcoder_.path) {
        return ffmpegVersionCache_;
    }
    ffmpegVersionProbed_ = true;
    ffmpegVersionPath_   = transcoder_.path;
    ffmpegVersionCache_.clear();
    if (!transcoder_.available || transcoder_.path.isEmpty()) return ffmpegVersionCache_;

    QProcess p;
    p.start(transcoder_.path, { QStringLiteral("-hide_banner"), QStringLiteral("-version") });
    if (p.waitForFinished(5000)) {
        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        // 取首行：形如 "ffmpeg version 8.0 Copyright (c) ..."
        ffmpegVersionCache_ = out.section(QLatin1Char('\n'), 0, 0).trimmed();
    } else {
        p.kill();
        p.waitForFinished(3000);
    }
    return ffmpegVersionCache_;
}

void PlayerController::clearTranscoderVersionCache() {
    ffmpegVersionProbed_ = false;
    ffmpegVersionPath_.clear();
    ffmpegVersionCache_.clear();
}

QString PlayerController::audioDeviceName() const {
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) return QStringLiteral("未找到音频输出设备");
    return dev.description();
}

void PlayerController::setAlwaysOnTop(bool v) {
    if (alwaysOnTop_ == v) return;
    alwaysOnTop_ = v;
    QSettings().setValue(QStringLiteral("window/alwaysOnTop"), v);
    emit alwaysOnTopChanged();
}

// ------------------------------------------------- 状态持久化

void PlayerController::attachWindow(QObject* window) {
    window_ = qobject_cast<QWindow*>(window);
}

QVariantMap PlayerController::windowGeometry() const {
    QVariantMap m;
    m.insert(QStringLiteral("valid"), false);
    m.insert(QStringLiteral("maximized"), false);

    QSettings s;
    const QRect g(s.value(QStringLiteral("window/geometry")).toRect());
    const bool maximized = s.value(QStringLiteral("window/maximized"), false).toBool();
    if (!g.isValid() || g.width() <= 0 || g.height() <= 0) return m;

    // QML 的 Window.x/y 是**客户区**坐标，而「窗口是否可见、能否拖动」取决于外框
    // （标题栏 + 边框）。两者相差 frameMargins()，只按客户区校验会把标题栏顶出屏幕。
    const QMargins fm = window_ ? window_->frameMargins() : QMargins(0, 0, 0, 0);
    const QRect frame(g.x() - fm.left(), g.y() - fm.top(),
                      g.width() + fm.left() + fm.right(),
                      g.height() + fm.top() + fm.bottom());

    // 校验几何是否仍「可用」。两点要求，缺一不可：
    //   1) 与某个屏幕的可用区有**足够大**的重叠——只判断 intersects() 太弱，
    //      哪怕只剩 1px 在屏内也会被判定为可用，用户同样看不到窗口；
    //   2) 不超过该屏幕可用区——否则底部控制条会落到任务栏之下。
    for (const QScreen* sc : QGuiApplication::screens()) {
        const QRect avail = sc->availableGeometry();
        const QRect vis = avail.intersected(frame);
        if (vis.width() < 200 || vis.height() < 120) continue;

        const int fw = qMin(frame.width(),  avail.width());
        const int fh = qMin(frame.height(), avail.height());
        const int fx = qBound(avail.left(), frame.x(), avail.right()  - fw + 1);
        const int fy = qBound(avail.top(),  frame.y(), avail.bottom() - fh + 1);

        m.insert(QStringLiteral("valid"), true);
        // 换算回客户区坐标交给 QML 应用
        m.insert(QStringLiteral("x"),      fx + fm.left());
        m.insert(QStringLiteral("y"),      fy + fm.top());
        m.insert(QStringLiteral("width"),  qMax(1, fw - fm.left() - fm.right()));
        m.insert(QStringLiteral("height"), qMax(1, fh - fm.top()  - fm.bottom()));
        m.insert(QStringLiteral("maximized"), maximized);
        break;
    }
    return m;
}

void PlayerController::saveWindowGeometry(int x, int y, int width, int height,
                                          bool maximized) {
    QSettings s;
    // 只保存“正常态”几何：全屏/最大化时不覆盖，否则下次启动会得到奇怪尺寸
    if (!maximized && width > 0 && height > 0) {
        s.setValue(QStringLiteral("window/geometry"), QRect(x, y, width, height));
    }
    s.setValue(QStringLiteral("window/maximized"), maximized);
}

void PlayerController::persistSettings() {
    QSettings s;
    s.setValue(QStringLiteral("audio/volume"), volume());
    s.setValue(QStringLiteral("audio/muted"), muted_);
    s.setValue(QStringLiteral("window/alwaysOnTop"), alwaysOnTop_);
    s.setValue(QStringLiteral("playback/loopMode"), loopMode_);
    s.setValue(QLatin1String(kSettingsRecent), recent_);
    s.sync();
}

// ------------------------------------------------- 播放列表

QVariantList PlayerController::playlist() const {
    QVariantList out;
    for (int i = 0; i < playlist_.size(); ++i) {
        QVariantMap m;
        m.insert(QStringLiteral("path"), playlist_.at(i));
        m.insert(QStringLiteral("name"), QFileInfo(playlist_.at(i)).fileName());
        m.insert(QStringLiteral("index"), i);
        m.insert(QStringLiteral("current"), i == playlistIndex_);
        m.insert(QStringLiteral("exists"), QFileInfo::exists(playlist_.at(i)));
        out.append(m);
    }
    return out;
}

void PlayerController::setLoopMode(const QString& mode) {
    if (mode != QLatin1String("none") && mode != QLatin1String("one") &&
        mode != QLatin1String("all")) return;
    if (loopMode_ == mode) return;
    loopMode_ = mode;
    QSettings().setValue(QStringLiteral("playback/loopMode"), mode);
    emit loopModeChanged();
}

void PlayerController::addToPlaylist(const QList<QUrl>& urls) {
    if (urls.isEmpty()) return;
    const bool wasEmpty = playlist_.isEmpty();
    for (const QUrl& u : urls) {
        const QString p = u.toLocalFile().isEmpty() ? u.toString() : u.toLocalFile();
        if (p.isEmpty()) continue;
        const QString abs = QFileInfo(p).absoluteFilePath();
        if (playlist_.contains(abs)) continue;   // 去重，避免重复拖入产生重复项
        playlist_ << abs;
    }
    if (playlist_.isEmpty()) return;
    emit playlistChanged();

    // 队列为空且当前没有媒体时，直接开始播放第一项
    if (wasEmpty && !hasMedia()) playlistPlayAt(0);
}

void PlayerController::playlistPlayAt(int index) {
    if (index < 0 || index >= playlist_.size()) return;
    playlistIndex_ = index;
    emit playlistChanged();
    open(playlist_.at(index));
}

void PlayerController::playlistRemoveAt(int index) {
    if (index < 0 || index >= playlist_.size()) return;
    const bool wasCurrent = (index == playlistIndex_);
    playlist_.removeAt(index);

    if (playlist_.isEmpty()) {
        playlistIndex_ = -1;
    } else if (wasCurrent) {
        // 移除正在播放项：顺延到同一位置（即原来的下一项）
        playlistIndex_ = qMin(index, playlist_.size() - 1);
    } else if (index < playlistIndex_) {
        --playlistIndex_;   // 前面的项被移除，当前索引整体前移
    }
    emit playlistChanged();
}

void PlayerController::playlistClear() {
    if (playlist_.isEmpty()) return;
    playlist_.clear();
    playlistIndex_ = -1;
    emit playlistChanged();
}

void PlayerController::playlistNext() {
    if (playlist_.size() < 2) return;
    playlistPlayAt((playlistIndex_ + 1) % playlist_.size());
}

void PlayerController::playlistPrevious() {
    if (playlist_.size() < 2) return;
    playlistPlayAt((playlistIndex_ - 1 + playlist_.size()) % playlist_.size());
}

// ------------------------------------------------- 最近打开

QVariantList PlayerController::recentFiles() const {
    QVariantList out;
    for (const auto& p : recent_) {
        QVariantMap m;
        m.insert(QStringLiteral("path"), p);
        m.insert(QStringLiteral("name"), QFileInfo(p).fileName());
        m.insert(QStringLiteral("exists"), QFileInfo::exists(p));
        out.append(m);
    }
    return out;
}

void PlayerController::pushRecent(const QString& path) {
    recent_.removeAll(path);
    recent_.prepend(path);
    while (recent_.size() > kMaxRecentFiles) recent_.removeLast();
    // 打开时即落盘：避免崩溃丢失本次记录
    QSettings().setValue(QLatin1String(kSettingsRecent), recent_);
    emit recentFilesChanged();
}

void PlayerController::clearRecentFiles() {
    if (recent_.isEmpty() && recentIssue_.isEmpty()) return;
    recent_.clear();
    recentIssue_.clear();
    QSettings().setValue(QLatin1String(kSettingsRecent), recent_);
    emit recentFilesChanged();
    emit recentIssueChanged();
}

// ------------------------------------------------- 实用功能

QString PlayerController::screenshotFilePath() const {
    QString dir = screenshotDirectory();
    // 目录被删掉（或权限变化）时尝试补建；仍不可用则退回默认目录，
    // 否则会出现「提示已保存」但文件其实没写出去。
    if (!QFileInfo(dir).isDir() && !QDir().mkpath(dir)) {
        dir = defaultScreenshotDirectory();
    }
    QString base = QFileInfo(fileName()).completeBaseName();
    if (base.isEmpty()) base = QStringLiteral("vtplay");
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    return QDir(dir).filePath(QStringLiteral("%1_%2.png").arg(base, stamp));
}

QString PlayerController::sourcePath() const {
    return QString::fromStdString(pipeline_->info().url);
}

void PlayerController::openContainingFolder(const QString& filePath) {
    if (filePath.isEmpty()) return;
    const QString dir = QFileInfo(filePath).absolutePath();
    if (!dir.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

} // namespace vtapp