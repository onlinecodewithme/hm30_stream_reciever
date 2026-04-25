#include "dashboard.h"
#include "video_widget.h"
#include "../core/stream_decoder.h"
#include "../core/app_config.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QStatusBar>
#include <QStyle>
#include <QDebug>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Dashboard::Dashboard(const QString &url, QWidget *parent)
    : QMainWindow(parent)
    , m_url(url)
{
    setWindowTitle(QStringLiteral("%1 (%2)")
                   .arg(QLatin1String(AppConfig::kWindowTitle))
                   .arg(m_url));
    setMinimumSize(AppConfig::kMinWindowWidth, AppConfig::kMinWindowHeight);
    showMaximized();

    buildUI();

    // Instantiate and wire the decode worker.
    m_decoder = new StreamDecoder(m_url, this);
    connect(m_decoder, &StreamDecoder::frameReady,
            m_video,   &VideoWidget::updateFrame);
    connect(m_decoder, &StreamDecoder::connectionChanged,
            m_video,   &VideoWidget::setConnectionStatus);
    connect(m_decoder, &StreamDecoder::connectionChanged,
            this,      &Dashboard::onConnectionChanged);

    // 2 Hz status refresh.
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &Dashboard::refreshStatus);
    m_statusTimer->start(AppConfig::kStatusIntervalMs);

    // Start the asynchronous decode pipeline.
    m_decoder->start();

    qInfo() << "[Dashboard] Initialized — connecting to" << m_url;
}

Dashboard::~Dashboard()
{
    if (m_decoder) {
        m_decoder->stop();
    }
}

// ---------------------------------------------------------------------------
// Private: UI construction
// ---------------------------------------------------------------------------

void Dashboard::buildUI()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    auto *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(12);

    mainLayout->addWidget(buildVideoPanel(), /*stretch=*/3);
    mainLayout->addWidget(buildInfoPanel(),  /*stretch=*/1);

    statusBar()->showMessage(
        QStringLiteral("SIYI HM30 RTP Receiver — Connecting to %1...").arg(m_url));
}

QWidget *Dashboard::buildVideoPanel()
{
    auto *container  = new QWidget;
    auto *layout     = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // -- Header row: LED · status label · stretch · FPS --
    auto *header = new QHBoxLayout;

    m_videoLed = new QLabel(QStringLiteral("●"));
    m_videoLed->setObjectName(QStringLiteral("videoLed"));
    m_videoLed->setFixedWidth(36);
    setLabelActive(m_videoLed, false);

    m_videoStatusLabel = new QLabel(QStringLiteral("VIDEO — Waiting for Stream..."));
    m_videoStatusLabel->setObjectName(QStringLiteral("videoStatusLabel"));
    setLabelActive(m_videoStatusLabel, false);

    m_fpsLabel = new QLabel;
    m_fpsLabel->setObjectName(QStringLiteral("fpsLabel"));

    header->addWidget(m_videoLed);
    header->addWidget(m_videoStatusLabel);
    header->addStretch();
    header->addWidget(m_fpsLabel);

    m_video = new VideoWidget(this);
    m_video->setUrlInfo(m_url);

    layout->addLayout(header);
    layout->addWidget(m_video);

    return container;
}

QWidget *Dashboard::buildInfoPanel()
{
    auto *panel  = new QWidget;
    panel->setMinimumWidth(AppConfig::kInfoPanelMinWidth);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // -- Connection status group --
    auto *connGroup  = new QGroupBox(QStringLiteral("⚡ CONNECTION STATUS"));
    auto *connLayout = new QVBoxLayout(connGroup);
    connLayout->setSpacing(8);
    m_connLabel = new QLabel(
        QStringLiteral("Video: Waiting for stream...\nMode: FFmpeg Direct Decode\nURL: %1").arg(m_url));
    m_connLabel->setObjectName(QStringLiteral("connLabel"));
    m_connLabel->setWordWrap(true);
    connLayout->addWidget(m_connLabel);
    layout->addWidget(connGroup);

    // -- Performance info group --
    auto *perfGroup  = new QGroupBox(QStringLiteral("⚡ PERFORMANCE (C++)"));
    auto *perfLayout = new QVBoxLayout(perfGroup);
    perfLayout->setSpacing(8);
    auto *perfInfo = new QLabel(
        QStringLiteral("• Direct FFmpeg decode (no OpenCV)\n"
                        "• Direct Stream Transport\n"
                        "• Asynchronous QThread decoupling\n"
                        "• Signal/Slot queued delivery\n"
                        "• QPainter direct render"));
    perfInfo->setObjectName(QStringLiteral("perfInfo"));
    perfInfo->setWordWrap(true);
    perfLayout->addWidget(perfInfo);
    layout->addWidget(perfGroup);

    // -- Stream info group --
    auto *streamGroup  = new QGroupBox(QStringLiteral("📡 STREAM INFO"));
    auto *streamLayout = new QVBoxLayout(streamGroup);
    streamLayout->setSpacing(8);
    auto *streamInfo = new QLabel(
        QStringLiteral("Mode: Stream Receiver\n"
                        "URL: %1\n"
                        "Codec: H.264 (FFmpeg direct)\n"
                        "Source: DJI Osmo Action 5 Pro\n"
                        "Link: Camera → HM30 Air → HM30 Ground → PC").arg(m_url));
    streamInfo->setObjectName(QStringLiteral("streamInfo"));
    streamInfo->setWordWrap(true);
    streamLayout->addWidget(streamInfo);
    layout->addWidget(streamGroup);

    layout->addStretch();
    return panel;
}

// ---------------------------------------------------------------------------
// Private: dynamic property helper
// ---------------------------------------------------------------------------

void Dashboard::setLabelActive(QLabel *label, bool active)
{
    label->setProperty("active", active);
    // Force the QStyle to re-evaluate QSS rules for this widget.
    style()->unpolish(label);
    style()->polish(label);
    label->update();
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void Dashboard::refreshStatus()
{
    if (!m_decoder) {
        return;
    }

    const int    w   = m_decoder->videoWidth();
    const int    h   = m_decoder->videoHeight();
    const double fps = m_decoder->fps();
    const bool   live = (w > 0 && h > 0);

    setLabelActive(m_videoLed,         live);
    setLabelActive(m_videoStatusLabel, live);

    if (live) {
        m_videoStatusLabel->setText(
            QStringLiteral("VIDEO — %1×%2 LIVE").arg(w).arg(h));
        m_fpsLabel->setText(
            QStringLiteral("%1 FPS").arg(fps, 0, 'f', 1));
        m_connLabel->setText(
            QStringLiteral("Video: ✅ Connected (%1×%2)\nFPS: %3\n"
                            "Codec: H.264 (FFmpeg)\nTransport: %4")
            .arg(w).arg(h).arg(fps, 0, 'f', 1).arg(m_url));
        statusBar()->showMessage(
            QStringLiteral("VID:✓  |  %1×%2 @ %3 FPS  |  %4  |  Direct FFmpeg")
            .arg(w).arg(h).arg(fps, 0, 'f', 1).arg(m_url));
    } else {
        m_videoStatusLabel->setText(QStringLiteral("VIDEO — No Signal"));
        m_fpsLabel->clear();
        m_connLabel->setText(
            QStringLiteral("Video: ❌ No stream\nConnecting to %1...\nWaiting for frames")
            .arg(m_url));
        statusBar()->showMessage(
            QStringLiteral("VID:✗  |  Connecting to %1...").arg(m_url));
    }
}

void Dashboard::onConnectionChanged(bool connected)
{
    Q_UNUSED(connected)
    // Immediately refresh rather than waiting for the next timer tick.
    refreshStatus();
}
