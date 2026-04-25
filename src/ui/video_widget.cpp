#include "video_widget.h"
#include "../core/app_config.h"

#include <QPainter>
#include <QFont>
#include <QFontDatabase>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
    , m_url(AppConfig::kDefaultUrl)
{
    setMinimumSize(AppConfig::kMinVideoWidth, AppConfig::kMinVideoHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Styling (background, border-radius) is controlled entirely by style.qss.
    // No inline setStyleSheet() calls here.
}

// ---------------------------------------------------------------------------
// Public slots
// ---------------------------------------------------------------------------

void VideoWidget::updateFrame(const QImage &frame)
{
    m_currentFrame = frame;
    update();
}

void VideoWidget::setConnectionStatus(bool connected)
{
    m_connected = connected;
    if (!m_connected) {
        m_currentFrame = QImage(); // Clear stale frame so placeholder is shown.
    }
    update();
}

void VideoWidget::setUrlInfo(const QString &url)
{
    m_url = url;
    update();
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void VideoWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    // Nearest-neighbour on live video avoids blurring; the GPU does the display scaling.
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const int w = width();
    const int h = height();

    if (!m_connected || m_currentFrame.isNull()) {
        paintNoSignal(p, w, h);
        return;
    }

    // Scale the frame to fit while preserving aspect ratio, then centre it.
    QSize scaledSize = m_currentFrame.size().scaled(w, h, Qt::KeepAspectRatio);
    const int x = (w - scaledSize.width())  / 2;
    const int y = (h - scaledSize.height()) / 2;

    p.fillRect(0, 0, w, h, QColor(1, 4, 9)); // Letterbox background.
    p.drawImage(QRect(x, y, scaledSize.width(), scaledSize.height()), m_currentFrame);
}

void VideoWidget::paintNoSignal(QPainter &p, int w, int h) const
{
    // Dark background.
    p.fillRect(0, 0, w, h, QColor(1, 4, 9));

    // Subtle checkerboard pattern.
    p.setPen(Qt::NoPen);
    const QColor checker(15, 15, 20);
    constexpr int kCellSize = 20;
    for (int row = 0; row < h; row += kCellSize) {
        for (int col = 0; col < w; col += kCellSize) {
            if ((col / kCellSize + row / kCellSize) % 2 == 0) {
                p.fillRect(col, row, kCellSize, kCellSize, checker);
            }
        }
    }

    // Primary "waiting" text — use system sans-serif so it degrades gracefully.
    QFont titleFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    titleFont.setPointSize(24);
    titleFont.setWeight(QFont::Bold);
    p.setFont(titleFont);
    p.setPen(QColor(60, 60, 80));
    p.drawText(QRect(0, 0, w, h), Qt::AlignCenter, QStringLiteral("WAITING FOR STREAM"));

    // Sub-text with port info.
    QFont subFont = titleFont;
    subFont.setPointSize(10);
    subFont.setWeight(QFont::Normal);
    p.setFont(subFont);
    p.setPen(QColor(40, 40, 60));
    p.drawText(QRect(0, h / 2 + 20, w, 30), Qt::AlignCenter,
               QStringLiteral("Connecting to %1...").arg(m_url));
}
