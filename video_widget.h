#pragma once

#include <QWidget>
#include <QImage>

/**
 * A lightweight view component strictly responsible for rendering video frames
 * received from a decoupled decoding engine.
 */
class VideoWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget() override = default;

public slots:
    void updateFrame(const QImage &frame);
    void setConnectionStatus(bool connected);
    void setPortInfo(int port);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void paintNoSignal(QPainter &p, int w, int h);

    QImage m_currentFrame;
    bool m_connected{false};
    int m_port{5600};
};
