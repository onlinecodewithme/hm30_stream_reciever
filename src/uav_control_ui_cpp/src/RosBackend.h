#ifndef ROS_BACKEND_H
#define ROS_BACKEND_H

#include <QObject>
#include <QString>
#include <QImage>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/float32.hpp>
#include <geometry_msgs/msg/point.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <map_msgs/msg/occupancy_grid_update.hpp>
#include <cognition_brain_interfaces/srv/drone_pad_control.hpp>
#include <QTimer>
#include <QVariantList>

class RosBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(int speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(int temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
    Q_PROPERTY(int battery READ battery WRITE setBattery NOTIFY batteryChanged)
    Q_PROPERTY(bool isAuto READ isAuto WRITE setIsAuto NOTIFY isAutoChanged)
    Q_PROPERTY(bool mappingEnabled READ mappingEnabled WRITE setMappingEnabled NOTIFY mappingEnabledChanged)
    Q_PROPERTY(double maxSpeed READ maxSpeed WRITE setMaxSpeed NOTIFY maxSpeedChanged)
    
    Q_PROPERTY(double panX READ panX NOTIFY panXChanged)
    Q_PROPERTY(double tiltY READ tiltY NOTIFY tiltYChanged)
    Q_PROPERTY(double linearSpeed READ linearSpeed NOTIFY linearSpeedChanged)
    Q_PROPERTY(double angularSpeed READ angularSpeed NOTIFY angularSpeedChanged)
    Q_PROPERTY(QString padStatus READ padStatus NOTIFY padStatusChanged)
    Q_PROPERTY(bool mainCameraOn READ mainCameraOn WRITE setMainCameraOn NOTIFY mainCameraOnChanged)

    Q_PROPERTY(QVariantList gridData READ gridData NOTIFY gridDataChanged)
    Q_PROPERTY(int gridWidth READ gridWidth NOTIFY gridWidthChanged)
    Q_PROPERTY(int gridHeight READ gridHeight NOTIFY gridHeightChanged)
    Q_PROPERTY(double robotX READ robotX NOTIFY robotXChanged)
    Q_PROPERTY(double robotY READ robotY NOTIFY robotYChanged)
    Q_PROPERTY(double robotAngle READ robotAngle NOTIFY robotAngleChanged)
    Q_PROPERTY(bool usingLiveData READ usingLiveData NOTIFY usingLiveDataChanged)

    Q_PROPERTY(int mapMinX READ mapMinX NOTIFY mapBoundsChanged)
    Q_PROPERTY(int mapMinY READ mapMinY NOTIFY mapBoundsChanged)
    Q_PROPERTY(int mapMaxX READ mapMaxX NOTIFY mapBoundsChanged)
    Q_PROPERTY(int mapMaxY READ mapMaxY NOTIFY mapBoundsChanged)

public:
    explicit RosBackend(QObject *parent = nullptr);
    ~RosBackend();

    void setNode(std::shared_ptr<rclcpp::Node> node);

    bool connected() const { return m_connected; }
    int speed() const { return m_speed; }
    int temperature() const { return m_temperature; }
    int battery() const { return m_battery; }
    bool isAuto() const { return m_isAuto; }
    bool mappingEnabled() const { return m_mappingEnabled; }
    double maxSpeed() const { return m_maxSpeed; }

    double panX() const { return m_panX; }
    double tiltY() const { return m_tiltY; }
    double linearSpeed() const { return m_linearSpeed; }
    double angularSpeed() const { return m_angularSpeed; }
    QString padStatus() const { return m_padStatus; }
    bool mainCameraOn() const { return m_mainCameraOn; }

    QVariantList gridData() const { return m_gridDataQ; }
    int gridWidth() const { return m_gridWidth; }
    int gridHeight() const { return m_gridHeight; }
    double robotX() const { return m_robotX; }
    double robotY() const { return m_robotY; }
    double robotAngle() const { return m_robotAngle; }
    bool usingLiveData() const { return m_usingLiveData; }

    int mapMinX() const { return m_mapMinX; }
    int mapMinY() const { return m_mapMinY; }
    int mapMaxX() const { return m_mapMaxX; }
    int mapMaxY() const { return m_mapMaxY; }

public slots:
    void setIsAuto(bool isAuto);
    void setMappingEnabled(bool enabled);
    void setMaxSpeed(double speed);
    void setTemperature(int temp);
    void setBattery(int batt);
    void setMainCameraOn(bool isOn);
    void updateLeftJoystick(double x, double y);
    void updateRightJoystick(double x, double y);
    void stopAction();
    void cameraAction();
    void targetAction();
    void gimbalHomeAction();
    void launchUAV();
    void cancelTakeoff();

signals:
    void newFrameReceived(const QImage &image);
    void connectedChanged();
    void speedChanged();
    void temperatureChanged();
    void batteryChanged();
    void isAutoChanged();
    void mappingEnabledChanged();
    void maxSpeedChanged();
    void panXChanged();
    void tiltYChanged();
    void linearSpeedChanged();
    void angularSpeedChanged();
    void padStatusChanged();
    void mainCameraOnChanged();
    
    void gridDataChanged();
    void gridWidthChanged();
    void gridHeightChanged();
    void robotXChanged();
    void robotYChanged();
    void robotAngleChanged();
    void usingLiveDataChanged();
    void mapBoundsChanged();

private slots:
    void publishCmdVel();

private:
    std::shared_ptr<rclcpp::Node> m_node;
    bool m_connected = false;
    int m_speed = 0;
    int m_temperature = 0;
    int m_battery = 0;
    bool m_isAuto = true;
    bool m_mappingEnabled = false;
    double m_maxSpeed = 0.1;

    // Joystick 2
    double m_linearSpeed = 0.0;
    double m_angularSpeed = 0.0;
    double m_rightJoyX = 0.0;
    double m_rightJoyY = 0.0;

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr m_imageSub;
    void imageCallback(const sensor_msgs::msg::Image::SharedPtr msg);

    // Drone Pad
    QString m_padStatus = "closed";
    rclcpp::Client<cognition_brain_interfaces::srv::DronePadControl>::SharedPtr m_padClient;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr m_padStatusSub;
    void padStatusCallback(const std_msgs::msg::String::SharedPtr msg);

    // Gimbal Control
    double m_panX = 2050; // DEFAULT_PAN
    double m_tiltY = -4000; // DEFAULT_TILT
    rclcpp::Publisher<geometry_msgs::msg::Point>::SharedPtr m_gimbalPosPub;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr m_gimbalHomePub;

    // Camera Pulse
    bool m_mainCameraOn = false;
    rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr m_cameraPulsePub;

    // Twist / Navigation Control
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr m_cmdVelPub;
    QTimer* m_cmdVelTimer = nullptr;

    // Map & Odometry
    QVariantList m_gridDataQ;
    std::vector<int8_t> m_gridDataRaw;
    int m_gridWidth = 0;
    int m_gridHeight = 0;
    double m_mapResolution = 0.05;
    double m_mapOriginX = 0.0;
    double m_mapOriginY = 0.0;
    double m_robotX = 0.0;
    double m_robotY = 0.0;
    double m_robotAngle = 0.0;
    bool m_usingLiveData = false;

    int m_mapMinX = 0;
    int m_mapMinY = 0;
    int m_mapMaxX = 0;
    int m_mapMaxY = 0;
    void computeMapBounds();

    rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr m_mapSub;
    rclcpp::Subscription<map_msgs::msg::OccupancyGridUpdate>::SharedPtr m_mapUpdateSub;
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr m_odomSub;

    void mapCallback(const nav_msgs::msg::OccupancyGrid::SharedPtr msg);
    void mapUpdateCallback(const map_msgs::msg::OccupancyGridUpdate::SharedPtr msg);
    void odomCallback(const nav_msgs::msg::Odometry::SharedPtr msg);
};

#endif // ROS_BACKEND_H
