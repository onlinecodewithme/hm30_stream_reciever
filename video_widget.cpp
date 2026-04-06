#include "video_widget.h"
#include <QPainter>
#include <QFont>

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(640, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Note: Border and styling are now intended to be set via external style.qss
}

void VideoWidget::updateFrame(const QImage &frame)
{
    m_currentFrame = frame;
    update(); // Schedule repaint immediately
}

void VideoWidget::setConnectionStatus(bool connected)
{
    m_connected = connected;
    if (!m_connected) {
        m_currentFrame = QImage(); 
    }
    update();
}

void VideoWidget::setPortInfo(int port)
{
    m_port = port;
    update();
}

void VideoWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    int w = width();
    int h = height();

    if (!m_connected || m_currentFrame.isNull()) {
        paintNoSignal(p, w, h);
        return;
    }

    QSize imgSize = m_currentFrame.size();
    imgSize.scale(w, h, Qt::KeepAspectRatio);
    int x = (w - imgSize.width()) / 2;
    int y = (h - imgSize.height()) / 2;

    p.fillRect(0, 0, w, h, QColor(1, 4, 9));
    p.drawImage(QRect(x, y, imgSize.width(), imgSize.height()), m_currentFrame);
}

void VideoWidget::paintNoSignal(QPainter &p, int w, int h)
{
    p.fillRect(0, 0, w, h, QColor(1, 4, 9));
    p.setPen(Qt::NoPen);
    QColor check(15, 15, 20);
    for (int y = 0; y < h; y += 20) {
        for (int x = 0; x < w; x += 20) {
            if ((x / 20 + y / 20) % 2 == 0) {
                p.fillRect(x, y, 20, 20, check);
            }
        }
    }

    QFont font("Inter", 24, QFont::Bold);
    p.setFont(font);
    p.setPen(QColor(60, 60, 80));
    p.drawText(QRect(0, 0, w, h), Qt::AlignCenter, "WAITING FOR RTP STREAM");

    QFont sub("Inter", 10);
    p.setFont(sub);
    p.setPen(QColor(40, 40, 60));
    p.drawText(QRect(0, h/2 + 20, w, 30), Qt::AlignCenter,
               QString("Listening on UDP port %1...").arg(m_port));
}
