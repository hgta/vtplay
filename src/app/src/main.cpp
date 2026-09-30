#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QUrl>

#include <cstdio>

#ifdef _WIN32
#  include <windows.h>
#endif

#include "PlayerController.h"
#include "VideoRenderer.h"
#include "AudioOutput.h"

namespace {

QMutex g_logMutex;
QFile  g_logFile;

QString logFilePath() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + QStringLiteral("/.vtplay");
    return QDir(dir).filePath(QStringLiteral("vtplay.log"));
}

/// 诊断信息同时写 stderr 与日志文件。
///
/// 为什么必须落盘：发布构建是窗口子系统，双击运行时 stderr 无人接收；
/// QML 加载失败、解码错误这类信息会在用户那里彻底消失（排查只能靠猜）。
/// 必须在使用任何 Qt 日志之前安装。
const char* levelName(QtMsgType t) {
    // 注意：不能用数组下标映射。QtInfoMsg 的枚举值是 4，排在 QtFatalMsg(3) 之后，
    // 按下标取会把 qInfo 标成 FATAL、把 qWarning 标成 INFO（曾实际发生）。
    switch (t) {
        case QtDebugMsg:    return "DEBUG";
        case QtInfoMsg:     return "INFO ";
        case QtWarningMsg:  return "WARN ";
        case QtCriticalMsg: return "ERROR";
        case QtFatalMsg:    return "FATAL";
    }
    return "INFO ";
}

void vtMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    const QString line = QStringLiteral("%1 [%2] %3\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             QString::fromLatin1(levelName(type)), msg);
    const QByteArray utf8 = line.toUtf8();

    QMutexLocker lock(&g_logMutex);
    std::fwrite(utf8.constData(), 1, size_t(utf8.size()), stderr);
    std::fflush(stderr);

    if (!g_logFile.isOpen()) {
        const QString p = logFilePath();
        if (!p.isEmpty()) {
            QDir().mkpath(QFileInfo(p).absolutePath());
            g_logFile.setFileName(p);
            // 打开失败（无写权限等）只降级为「没有日志文件」，不影响运行
            (void)g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        }
    }
    if (g_logFile.isOpen()) {
        g_logFile.write(utf8);
        g_logFile.flush();
    }
}

/// 日志超过 1MB 时轮转一次（只保留上一份），避免无限增长。
void rotateLogIfNeeded() {
    const QString p = logFilePath();
    QFileInfo fi(p);
    if (!fi.exists() || fi.size() < 1024 * 1024) return;
    const QString old = p + QStringLiteral(".1");
    QFile::remove(old);
    QFile::rename(p, old);
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // 发布构建使用窗口子系统（双击不弹控制台）。设置 VTPLAY_CONSOLE=1 可附加
    // 控制台以查看诊断输出——这是"去掉黑窗"后保留可诊断性的逃生通道。
    if (qgetenv("VTPLAY_CONSOLE") == "1") {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
#endif

    QGuiApplication app(argc, argv);
    app.setOrganizationName("VTPlay");
    app.setApplicationName("VTPlay");

    // 日志必须在创建任何 Qt 对象前安装（组织名已设置，日志路径才有意义）
    rotateLogIfNeeded();
    qInstallMessageHandler(vtMessageHandler);

    // 启动横幅：便于确认运行的是哪个构建（排查"跑的到底是不是最新版"）。
    // 版本号来自 CMake 的 project(... VERSION ...)，单一来源。
    std::fprintf(stderr,
        "=== VTPlay %s ===\n"
        "build: %s %s\n"
        "qt: %s\n",
#ifndef VTPLAY_VERSION
        "dev",
#else
        VTPLAY_VERSION,
#endif
        __DATE__, __TIME__,
        qVersion());
    std::fflush(stderr);

    QQuickStyle::setStyle("Basic");

    qmlRegisterUncreatableType<vtapp::PlayerController>("VTPlay", 1, 0, "PlayerController",
        "PlayerController is owned by C++");
    qmlRegisterUncreatableType<vtapp::AudioOutput>("VTPlay", 1, 0, "AudioOutput",
        "Created by Main.qml");
    // VideoRenderer 用独立 URI 注册，避免与 qml 模块的 qmldir（目录式导入）冲突
    qmlRegisterType<vtapp::VideoRenderer>("VTPlayCore", 1, 0, "VideoRenderer");

    vtapp::PlayerController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("player", &controller);
    engine.rootContext()->setContextProperty("appWin", engine.rootObjects().value(0));

    engine.loadFromModule("VTPlay", "Main");
    if (engine.rootObjects().isEmpty()) {
        // 明确报出来：否则窗口子系统下表现为「双击没反应」，无从下手
        qCritical("QML 根对象创建失败，界面无法显示（详见上方 QML 错误）");
        return -1;
    }

    // 命令行传入媒体文件则直接打开（open 内部会自动开始播放）
    if (argc > 1 && argv[1] && *argv[1]) {
        controller.openUrl(QUrl::fromUserInput(QString::fromLocal8Bit(argv[1])));
    }

    // 退出路径埋点：窗口子系统下「点了关闭但进程不退出」很难从外部判断卡在哪一步
    QObject::connect(&app, &QGuiApplication::aboutToQuit, []() {
        qInfo("[app] aboutToQuit");
    });
    const int rc = app.exec();
    qInfo("[app] exec returned %d", rc);
    return rc;
}