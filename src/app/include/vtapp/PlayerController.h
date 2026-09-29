#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <memory>

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
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)

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

public slots:
    void openUrl(const QUrl& url);
    void open(const QString& path);
    void play();
    void pause();
    void togglePlay();
    void stop();
    void seek(double sec);
    void seekDelta(double sec);
    void stepFrame();
    void toggleFullscreen();

    /// QML 里 VideoRenderer 实例创建后调用，把渲染项挂接到管线。
    void attachRenderer(QObject* renderer);

    /// QML 传入的最新一帧；VideoRenderer 负责从管线拉取并显示。
    QObject* renderer() const;
    QObject* audio() const;

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

private:
    std::unique_ptr<vtcore::MediaPipeline> pipeline_;
    class VideoRenderer*    renderer_ = nullptr;
    class AudioOutput*       audio_ = nullptr;
    bool    muted_ = false;
    double  prevVolume_ = 1.0;
    bool    fullscreen_ = false;
    QString errorString_;
};

} // namespace vtapp