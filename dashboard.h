#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include "video_widget.h"
#include "stream_decoder.h"

/**
 * Main dashboard window for the RTP H.264 receiver.
 * Manages the UI view (VideoWidget) and connects it to the decoding engine (StreamDecoder).
 */
class Dashboard : public QMainWindow {
    Q_OBJECT

public:
    explicit Dashboard(int port = 5600, QWidget *parent = nullptr);
    ~Dashboard() override;

private slots:
    void updateStatus();
    void onConnectionChanged(bool connected);

private:
    void buildUI();

    VideoWidget *m_video;
    StreamDecoder *m_decoder;
    int m_port;

    // Status widgets
    QLabel *m_videoStatusLabel;
    QLabel *m_fpsLabel;
    QLabel *m_videoLed;
    QLabel *m_connLabel;

    QTimer *m_statusTimer;
};
