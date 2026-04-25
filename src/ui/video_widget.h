#pragma once

#include <QWidget>
#include <QImage>

/**
 * @class VideoWidget
 * @brief Lightweight view component responsible solely for rendering decoded video frames.
 *
 * VideoWidget is a pure display primitive — it holds no decoding logic.
 * It receives pre-decoded @c QImage frames through the @c updateFrame() slot
 * and repaints itself via Qt's @c update() / @c paintEvent() cycle.
 *
 * ### Responsibilities
 * - Draw the latest frame, aspect-ratio-correct, centred inside its bounds.
 * - Render a "no signal" placeholder screen when disconnected.
 *
 * ### Non-responsibilities
 * - Any form of decoding, buffering, or thread management.
 * - UI chrome (status labels, group boxes) — those belong to Dashboard.
 */
class VideoWidget : public QWidget {
    Q_OBJECT

public:
    /** @brief Constructs the video canvas with its minimum size policy applied. */
    explicit VideoWidget(QWidget *parent = nullptr);

    ~VideoWidget() override = default;

public slots:
    /**
     * @brief Slot: receives a newly decoded frame and schedules a repaint.
     * @param frame An RGB888 QImage produced by StreamDecoder. Must be valid.
     *              This slot is typically connected via a Qt::QueuedConnection.
     */
    void updateFrame(const QImage &frame);

    /**
     * @brief Slot: updates the connection state and repaints.
     * @param connected @c true while the stream is alive, @c false on drop/disconnect.
     *                  When set to @c false the current frame buffer is cleared so that
     *                  the placeholder is drawn instead of a frozen last frame.
     */
    void setConnectionStatus(bool connected);

    /**
     * @brief Informs the widget which URL is being watched.
     *        Used purely for the "waiting" overlay text.
     */
    void setUrlInfo(const QString &url);

protected:
    /** @brief Qt paint event: renders either the live frame or the no-signal screen. */
    void paintEvent(QPaintEvent *event) override;

private:
    /**
     * @brief Renders the animated checkerboard "no signal" placeholder.
     * @param p Reference to the active @c QPainter.
     * @param w Widget width in pixels.
     * @param h Widget height in pixels.
     */
    void paintNoSignal(QPainter &p, int w, int h) const;

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    QImage m_currentFrame;       ///< Last valid decoded frame. Null when disconnected.
    bool   m_connected{false};   ///< True while the stream is active.
    QString m_url;               ///< URL shown in the placeholder overlay.
};
