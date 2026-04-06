#include "dashboard.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFont>
#include <QStatusBar>
#include <QApplication>
#include <QFile>
#include <QCoreApplication>
#include <QDebug>

Dashboard::Dashboard(int port, QWidget *parent)
    : QMainWindow(parent), m_port(port)
{
    setWindowTitle(QString("SIYI HM30 — RTP Receiver (port %1)").arg(m_port));
    setMinimumSize(1000, 600);
    showMaximized();

    buildUI();

    // Find the SDP template gracefully checking binary dir then root
    QString sdpPath = QCoreApplication::applicationDirPath() + "/stream.sdp";
    if (!QFile::exists(sdpPath)) {
        sdpPath = QCoreApplication::applicationDirPath() + "/../stream.sdp";
    }

    // Initialize backend worker thread
    m_decoder = new StreamDecoder(m_port, sdpPath, this);
    
    // Connect Signals from Decoder -> View & Dashboard 
    // This perfectly isolates thread-safety locking requirements.
    connect(m_decoder, &StreamDecoder::frameReady, m_video, &VideoWidget::updateFrame);
    connect(m_decoder, &StreamDecoder::connectionChanged, m_video, &VideoWidget::setConnectionStatus);
    connect(m_decoder, &StreamDecoder::connectionChanged, this, &Dashboard::onConnectionChanged);

    // Status update timer @ 2 Hz
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &Dashboard::updateStatus);
    m_statusTimer->start(500);

    // Ignite the asynchronous decode layer
    m_decoder->start();
}

Dashboard::~Dashboard()
{
    if (m_decoder) {
        m_decoder->stop();
    }
}

void Dashboard::buildUI()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QHBoxLayout *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(12);

    // ── Left: Video panel ──
    QWidget *videoContainer = new QWidget;
    QVBoxLayout *videoLayout = new QVBoxLayout(videoContainer);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->setSpacing(8);

    // Video header
    QHBoxLayout *videoHeader = new QHBoxLayout;
    m_videoLed = new QLabel("●");
    m_videoLed->setObjectName("videoLed"); // Tagged for Stylesheet styling
    m_videoLed->setFixedWidth(36);
    m_videoStatusLabel = new QLabel("VIDEO — Waiting for RTP...");
    m_videoStatusLabel->setObjectName("videoStatusLabel");
    m_fpsLabel = new QLabel("");
    m_fpsLabel->setObjectName("fpsLabel");
    videoHeader->addWidget(m_videoLed);
    videoHeader->addWidget(m_videoStatusLabel);
    videoHeader->addStretch();
    videoHeader->addWidget(m_fpsLabel);
    videoLayout->addLayout(videoHeader);

    // Video widget dynamically linked
    m_video = new VideoWidget(this);
    m_video->setPortInfo(m_port);
    videoLayout->addWidget(m_video);

    mainLayout->addWidget(videoContainer, 3);

    // ── Right: Info panel ──
    QWidget *rightPanel = new QWidget;
    rightPanel->setMinimumWidth(360);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    // Connection status group
    QGroupBox *connGroup = new QGroupBox("⚡ CONNECTION STATUS");
    QVBoxLayout *connLayout = new QVBoxLayout(connGroup);
    connLayout->setSpacing(8);
    m_connLabel = new QLabel(
        QString("Video: Waiting for stream...\n"
                "Mode: RTP/UDP Receiver\n"
                "Port: %1").arg(m_port));
    m_connLabel->setObjectName("connLabel");
    m_connLabel->setWordWrap(true);
    connLayout->addWidget(m_connLabel);
    rightLayout->addWidget(connGroup);

    // Performance info group
    QGroupBox *perfGroup = new QGroupBox("⚡ PERFORMANCE (C++)");
    QVBoxLayout *perfLayout = new QVBoxLayout(perfGroup);
    perfLayout->setSpacing(8);
    QLabel *perfInfo = new QLabel(
        "• Direct FFmpeg decode (no OpenCV)\n"
        "• Raw RTP/UDP (no RTSP overhead)\n"
        "• Asynchronous QThread decoupling\n"
        "• Signal/Slot event queuing\n"
        "• QPainter direct render"
    );
    perfInfo->setObjectName("perfInfo");
    perfInfo->setWordWrap(true);
    perfLayout->addWidget(perfInfo);
    rightLayout->addWidget(perfGroup);

    // Stream info group
    QGroupBox *streamGroup = new QGroupBox("📡 STREAM INFO");
    QVBoxLayout *streamLayout = new QVBoxLayout(streamGroup);
    streamLayout->setSpacing(8);
    QLabel *streamInfo = new QLabel(
        QString("Mode: RTP/UDP Receiver\n"
                "Listen Port: %1\n"
                "Codec: H.264 (camera passthrough fallback safe)\n"
                "Source: DJI Osmo Action 5 Pro\n"
                "Link: Camera → GStreamer → HM30 → PC").arg(m_port)
    );
    streamInfo->setObjectName("streamInfo");
    streamInfo->setWordWrap(true);
    streamLayout->addWidget(streamInfo);
    rightLayout->addWidget(streamGroup);

    rightLayout->addStretch();
    mainLayout->addWidget(rightPanel, 1);

    // Status bar
    statusBar()->showMessage(
        QString("SIYI HM30 RTP Receiver — Listening on UDP port %1...").arg(m_port));
}

void Dashboard::updateStatus()
{
    if (m_decoder && m_decoder->videoWidth() > 0) {
        int w = m_decoder->videoWidth();
        int h = m_decoder->videoHeight();
        double fps = m_decoder->fps();

        m_videoLed->setStyleSheet("color: #3fb950; font-size: 24pt;");
        m_videoStatusLabel->setText(QString("VIDEO — %1×%2 LIVE (RTP)").arg(w).arg(h));
        m_videoStatusLabel->setStyleSheet("color: #3fb950; font-size: 14pt; font-weight: bold;");
        m_fpsLabel->setText(QString("%1 FPS").arg(fps, 0, 'f', 1));

        m_connLabel->setText(
            QString("Video: ✅ Connected (%1×%2)\n"
                    "FPS: %3\n"
                    "Codec: H.264 (FFmpeg direct)\n"
                    "Transport: RTP/UDP port %4").arg(w).arg(h)
                .arg(fps, 0, 'f', 1).arg(m_port));

        statusBar()->showMessage(
            QString("VID:✓  |  %1×%2 @ %3 FPS  |  "
                    "RTP/UDP port %4  |  Direct FFmpeg")
            .arg(w).arg(h).arg(fps, 0, 'f', 1).arg(m_port));
    } else {
        m_videoLed->setStyleSheet("color: #f85149; font-size: 24pt;");
        m_videoStatusLabel->setText("VIDEO — No Signal");
        m_videoStatusLabel->setStyleSheet("color: #f85149; font-size: 14pt; font-weight: bold;");
        m_fpsLabel->setText("");
        m_connLabel->setText(
            QString("Video: ❌ No stream\n"
                    "Listening on UDP port %1...\n"
                    "Waiting for RTP packets").arg(m_port));
        statusBar()->showMessage(
            QString("VID:✗  |  Listening on UDP port %1...").arg(m_port));
    }
}

void Dashboard::onConnectionChanged(bool connected)
{
    updateStatus();
}
