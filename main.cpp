#include <QApplication>
#include <QCoreApplication>
#include <QScopedPointer>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDateTime>
#include <QFile>
#include <QDebug>
#include <iostream>
#include <signal.h>

#include "src/core/app_config.h"
#include "src/ui/dashboard.h"
#include "src/core/stream_decoder.h"

#ifdef WITH_ROS2
#include "src/ros2/ros2_bridge.h"
#include <rclcpp/rclcpp.hpp>
#include <thread>
#endif

// ---------------------------------------------------------------------------
// Structured logging — installed as the global Qt message handler.
// Prints ISO-8601 timestamps so logs are trivially parseable (e.g. by systemd).
// ---------------------------------------------------------------------------
static void structuredLogHandler(QtMsgType type,
                                 const QMessageLogContext & /*ctx*/,
                                 const QString &msg)
{
    const QByteArray localMsg  = msg.toLocal8Bit();
    const QByteArray timeStamp =
        QDateTime::currentDateTime()
        .toString(Qt::ISODateWithMs).toLocal8Bit();
    const char *ts = timeStamp.constData();
    const char *m  = localMsg.constData();

    switch (type) {
    case QtDebugMsg:    fprintf(stdout, "[%s] [DEBUG] %s\n", ts, m); break;
    case QtInfoMsg:     fprintf(stdout, "[%s] [INFO]  %s\n", ts, m); break;
    case QtWarningMsg:  fprintf(stderr, "[%s] [WARN]  %s\n", ts, m); break;
    case QtCriticalMsg: fprintf(stderr, "[%s] [CRIT]  %s\n", ts, m); break;
    case QtFatalMsg:
        fprintf(stderr, "[%s] [FATAL] %s\n", ts, m);
        abort();
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    qInstallMessageHandler(structuredLogHandler);

    // Honour default SIGINT behaviour so Ctrl-C terminates the process.
    signal(SIGINT, SIG_DFL);

#ifdef WITH_ROS2
    rclcpp::init(argc, argv);
    auto rosNode = std::make_shared<rclcpp::Node>("hm30_receiver_node");
    
    // Run ROS spinner in a background thread so it doesn't block Qt
    std::thread rosSpinner([rosNode]() {
        rclcpp::spin(rosNode);
    });
#endif

    bool isHeadless = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) {
            isHeadless = true;
            break;
        }
    }

    QScopedPointer<QCoreApplication> app;
    if (isHeadless) {
        app.reset(new QCoreApplication(argc, argv));
    } else {
        app.reset(new QApplication(argc, argv));
    }

    app->setApplicationName(QLatin1String(AppConfig::kAppName));
    app->setApplicationVersion(QLatin1String(AppConfig::kAppVersion));

    qInfo() << "=== SIYI HM30 RTP Receiver" << AppConfig::kAppVersion << "===";

    // -- Command-line interface -----------------------------------------------
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Production-grade H.264 RTP/UDP receiver and dashboard for the SIYI HM30."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption urlOption(
        {QStringLiteral("u"), QStringLiteral("url")},
        QStringLiteral("Stream URL to connect to (default: %1).").arg(QLatin1String(AppConfig::kDefaultUrl)),
        QStringLiteral("url"),
        QLatin1String(AppConfig::kDefaultUrl));
    parser.addOption(urlOption);

    const QCommandLineOption headlessOption(
        {QStringLiteral("headless")},
        QStringLiteral("Run without the GUI (useful for ROS 2 background publisher)."));
    parser.addOption(headlessOption);

    parser.process(*app);

    const QString url = parser.value(urlOption);
    if (url.isEmpty()) {
        qCritical() << "Stream URL cannot be empty.";
        return 1;
    }

    qInfo() << "Using stream URL:" << url;

    // -- Launch ---------------------------------------------------------------
    StreamDecoder* decoder = nullptr;
    Dashboard* dashboard = nullptr;

    if (isHeadless) {
        qInfo() << "Running in HEADLESS mode.";
        decoder = new StreamDecoder(url);
        decoder->start();
    } else {
        // -- Stylesheet loading (embedded Qt resource) ------------------------
        QFile styleFile(QLatin1String(AppConfig::kStylesheetResource));
        if (styleFile.open(QFile::ReadOnly)) {
            static_cast<QApplication*>(app.data())->setStyleSheet(QLatin1String(styleFile.readAll()));
            styleFile.close();
            qInfo() << "Stylesheet applied from Qt resource.";
        } else {
            qWarning() << "Failed to load stylesheet — dashboard may render unstyled.";
        }

        dashboard = new Dashboard(url);
        dashboard->show();
        decoder = dashboard->decoder();
    }

#ifdef WITH_ROS2
    // Wire decoder directly to ROS Bridge
    Ros2Bridge rosBridge(rosNode);
    QObject::connect(decoder, &StreamDecoder::frameReady,
                     &rosBridge, &Ros2Bridge::onFrameReady);
#endif

    int ret = app->exec();

#ifdef WITH_ROS2
    rclcpp::shutdown();
    if (rosSpinner.joinable()) {
        rosSpinner.join();
    }
#endif

    return ret;
}
