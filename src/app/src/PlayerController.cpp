#include "PlayerController.h"
#include "VideoRenderer.h"
#include "AudioOutput.h"

#include "MediaPipeline.h"
#include "MediaInfo.h"
#include "PlayerState.h"

#include <QDebug>
#include <QFileInfo>

namespace vtapp {

PlayerController::PlayerController(QObject* parent) : QObject(parent) {
    pipeline_ = std::make_unique<vtcore::MediaPipeline>();
    audio_    = new AudioOutput(this);
    audio_->attach(pipeline_.get());
    // renderer 由 QML 创建，attachRenderer() 时挂接。
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
        pipeline_->open(fi.absoluteFilePath().toStdString());
        emit statusChanged();
        emit durationChanged();
        emit mediaInfoChanged();
    } catch (const std::exception& e) {
        errorString_ = QString::fromUtf8(e.what());
        emit errorChanged();
    }
}

void PlayerController::play() {
    pipeline_->play();
    emit statusChanged();
}
void PlayerController::pause() {
    pipeline_->pause();
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
    emit statusChanged();
    emit durationChanged();
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

void PlayerController::stepFrame() {
    pipeline_->stepFrame();
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

} // namespace vtapp