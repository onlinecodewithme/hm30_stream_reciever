#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QTimer>

// Forward declarations — avoids pulling the full headers into every TU that
// includes dashboard.h.
class VideoWidget;
class StreamDecoder;

/**
 * @class Dashboard
 * @brief Top-level application window for the SIYI HM30 RTP receiver.
 *
 * Dashboard owns both the UI layer (@c VideoWidget and status widgets) and the
 * decode worker (@c StreamDecoder).  It wires them together via Qt signals and
 * keeps the UI updated at a fixed refresh rate.
 *
 * ### Layout
 * ```
 * ┌───────────────────────────────┬──────────────────┐
 * │  VideoWidget (stretch=3)      │  Info Panel      │
 * │  • Live H.264 frame           │  • Connection    │
 * │  • No-signal placeholder      │  • Performance   │
 * │                               │  • Stream info   │
 * └───────────────────────────────┴──────────────────┘
 * [  Status Bar                                       ]
 * ```
 *
 * ### Styling
 * All colours, fonts and spacing are controlled exclusively by `style.qss`
 * via Qt dynamic properties (`active="true"/"false"`).  No @c setStyleSheet()
 * calls are made inside C++ at runtime.
 */
class Dashboard : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Constructs the dashboard and starts the decode worker.
     * @param url    Stream URL the receiver will connect to.
     * @param parent Optional Qt parent widget.
     */
    explicit Dashboard(const QString &url, QWidget *parent = nullptr);

    /** @brief Stops the decode worker cleanly before destruction. */
    ~Dashboard() override;

    /** @brief Expose the stream decoder for external signal connections (e.g. ROS 2 bridge). */
    StreamDecoder* decoder() const { return m_decoder; }

private slots:
    /**
     * @brief Periodic status refresh (invoked by @c m_statusTimer at 2 Hz).
     *        Reads atomic stats from StreamDecoder and updates the info panel.
     */
    void refreshStatus();

    /**
     * @brief Reacts to stream connect/disconnect events emitted by StreamDecoder.
     * @param connected @c true when the stream becomes active, @c false on loss.
     */
    void onConnectionChanged(bool connected);

private:
    // -------------------------------------------------------------------------
    // UI construction helpers
    // -------------------------------------------------------------------------

    /** @brief Creates and assembles the complete widget hierarchy. */
    void buildUI();

    /** @brief Creates the video canvas panel (left column). */
    QWidget *buildVideoPanel();

    /** @brief Creates the information side panel (right column). */
    QWidget *buildInfoPanel();

    // -------------------------------------------------------------------------
    // Dynamic property helpers (QSS-driven styling)
    // -------------------------------------------------------------------------

    /**
     * @brief Sets the "active" Qt dynamic property on a label and re-polishes
     *        it in the QStyle so the QSS selector picks up the change immediately.
     * @param label  The label whose property is to be updated.
     * @param active New value for the `active` property.
     */
    void setLabelActive(QLabel *label, bool active);

    // -------------------------------------------------------------------------
    // Core objects
    // -------------------------------------------------------------------------
    VideoWidget   *m_video{nullptr};    ///< Video canvas.
    StreamDecoder *m_decoder{nullptr};  ///< Background decode worker.
    QString        m_url;               ///< Stream URL (set at construction).

    // -------------------------------------------------------------------------
    // Status / info panel widgets
    // -------------------------------------------------------------------------
    QLabel *m_videoLed{nullptr};         ///< Coloured dot (green/red) via QSS.
    QLabel *m_videoStatusLabel{nullptr}; ///< "VIDEO — 1920×1080 LIVE" etc.
    QLabel *m_fpsLabel{nullptr};         ///< Current FPS reading.
    QLabel *m_connLabel{nullptr};        ///< Detailed connection info block.

    // -------------------------------------------------------------------------
    // Timers
    // -------------------------------------------------------------------------
    QTimer *m_statusTimer{nullptr};  ///< 2 Hz refresh timer.
};
