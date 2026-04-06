#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <iostream>
#include <signal.h>
#include "dashboard.h"

// ── Structured Logging Handler ──
void structuredLogHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    
    switch (type) {
    case QtDebugMsg:    fprintf(stdout, "[%s] [DEBUG] %s\n", timeStr.toLocal8Bit().constData(), localMsg.constData()); break;
    case QtInfoMsg:     fprintf(stdout, "[%s] [INFO]  %s\n", timeStr.toLocal8Bit().constData(), localMsg.constData()); break;
    case QtWarningMsg:  fprintf(stderr, "[%s] [WARN]  %s\n", timeStr.toLocal8Bit().constData(), localMsg.constData()); break;
    case QtCriticalMsg: fprintf(stderr, "[%s] [CRIT]  %s\n", timeStr.toLocal8Bit().constData(), localMsg.constData()); break;
    case QtFatalMsg:    fprintf(stderr, "[%s] [FATAL] %s\n", timeStr.toLocal8Bit().constData(), localMsg.constData()); abort();
    }
}

int main(int argc, char *argv[])
{
    // Install global high-performance production logger
    qInstallMessageHandler(structuredLogHandler);

    qInfo() << "=== SIYI HM30 RTP Receiver Initializing ===";

    // Graceful sigkill hooks
    signal(SIGINT, SIG_DFL);

    QApplication app(argc, argv);
    app.setApplicationName("hm30_rtp_receiver");
    app.setApplicationVersion("1.0 Production");

    // ── Command Line Interface ──
    QCommandLineParser parser;
    parser.setApplicationDescription("Production-grade H.264 RTP/UDP receiver and dashboard.");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption portOption(QStringList() << "p" << "port",
            QCoreApplication::translate("main", "UDP Port to listen on (default: 5600)."),
            QCoreApplication::translate("main", "port"), "5600");
    parser.addOption(portOption);

    parser.process(app);

    bool ok;
    int port = parser.value(portOption).toInt(&ok);
    if (!ok || port <= 0 || port > 65535) {
        qCritical() << "Invalid port specified. Exiting.";
        return 1;
    }

    qInfo() << "Binding listener to UDP port parameters:" << port;

    // Load External Qt Styling (UI decoupling)
    QFile styleFile(":/style.qss");
    if (!styleFile.exists()) {
        // Fallback to local loading if resource missing
        styleFile.setFileName(QCoreApplication::applicationDirPath() + "/style.qss");
        if (!styleFile.exists()) {
            styleFile.setFileName(QCoreApplication::applicationDirPath() + "/../style.qss");
        }
    }
    
    if (styleFile.open(QFile::ReadOnly)) {
        QString style = QLatin1String(styleFile.readAll());
        app.setStyleSheet(style);
        styleFile.close();
        qInfo() << "Successfully applied CSS stylesheet.";
    } else {
        qWarning() << "Failed to load stylesheet. Dashboard may render poorly.";
    }

    Dashboard dashboard(port);
    dashboard.show();

    return app.exec();
}
