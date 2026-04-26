import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import CustomControls 1.0

Item {
    id: dashboardRoot
    anchors.fill: parent

    property int currentTabIndex: 3

    Rectangle {
        anchors.fill: parent
        color: "#0F1115"
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        Column {
            width: parent.width - 40
            x: 20
            topPadding: 20
            spacing: 20

            // -- FORMER SECOND HALF NOW AT TOP --
            // Top bar of second half
            RowLayout {
                width: parent.width
                spacing: 15

                Rectangle { height: 40; width: row1.width + 30; radius: 8; color: "#2A2D35"; border.color: "#F4D03F"
                    Row { id: row1; anchors.centerIn: parent; spacing: 10
                        Text { text: "🚚 Xavier-RGV-311"; color: "white"; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                        Rectangle { width: 50; height: 22; radius: 6; color: "#F4D03F"; anchors.verticalCenter: parent.verticalCenter; Text { text: "Active"; color: "black"; font.pixelSize: 11; font.bold: true; anchors.centerIn: parent } }
                    }
                }

                Rectangle { height: 40; width: row2.width + 30; radius: 8; color: "#2A2D35"
                    Row { id: row2; anchors.centerIn: parent; spacing: 10
                        Text { text: "🚁 Xavier Beak 2.0"; color: "white"; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                        Rectangle { width: 50; height: 22; radius: 6; color: "#2ECC71"; anchors.verticalCenter: parent.verticalCenter; Text { text: "Active"; color: "white"; font.pixelSize: 11; font.bold: true; anchors.centerIn: parent } }
                    }
                }

                Item { Layout.fillWidth: true }

                Rectangle { width: 140; height: 40; radius: 8; color: "#2A2D35"; border.color: "#333"
                    Text { text: "⏸ Pause Inspection"; color: "white"; font.pixelSize: 12; anchors.centerIn: parent }
                }

                Rectangle { width: 140; height: 40; radius: 8; color: "#E74C3C"
                    Text { text: "⏹ Stop Inspection"; color: "white"; font.pixelSize: 12; font.bold: true; anchors.centerIn: parent }
                }
            }

            // Video and Telemetry Area
            RowLayout {
                width: parent.width
                height: 400
                spacing: 20

                // Video Placeholder
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#161925" // dark blueish
                    radius: 12
                    border.color: "#222"
                    clip: true

                    VideoStreamItem {
                        anchors.fill: parent
                        targetBackend: rosBackend
                    }

                    // Top Left Button
                    Rectangle {
                        width: 120; height: 30; radius: 8; color: "#222"
                        anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 15
                        Text { text: "📈 View 3D Model"; color: "white"; font.pixelSize: 12; anchors.centerIn: parent }
                    }

                    // Top Right Icons
                    Row {
                        anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 15
                        spacing: 10
                        // Gamepad Icon (NAVIGATION TO JOYSTICK UI)
                        Rectangle {
                            width: 30; height: 30; radius: 6; color: "#2A2D35"
                            Text { text: "🎮"; color: "white"; anchors.centerIn: parent }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: stackView.push("qrc:/qml/JoystickView.qml")
                            }
                        }
                        Rectangle { width: 30; height: 30; radius: 6; color: "#2A2D35"; Text { text: "🖥"; color: "white"; anchors.centerIn: parent } }
                        Rectangle {
                            width: 30; height: 30; radius: 6
                            color: rosBackend.mainCameraOn ? "#F4D03F" : "#2A2D35"
                            Text { text: "📷"; color: rosBackend.mainCameraOn ? "black" : "white"; anchors.centerIn: parent }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: rosBackend.mainCameraOn = !rosBackend.mainCameraOn
                            }
                        }
                    }

                    // Bottom Left
                    Rectangle {
                        width: 140; height: 40; radius: 8; color: "black"
                        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 15
                        Row { anchors.centerIn: parent; spacing: 10
                            Column {
                                Text { text: "⏱ Start"; color: "#888"; font.pixelSize: 10 }
                                Text { text: "08:30 am"; color: "white"; font.pixelSize: 12; font.bold: true }
                            }
                            Column {
                                Text { text: "⌛ Duration"; color: "#888"; font.pixelSize: 10 }
                                Text { text: "00h 24m 34s"; color: "white"; font.pixelSize: 12; font.bold: true }
                            }
                        }
                    }

                    // Bottom Right
                    Column {
                        anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.margins: 15
                        spacing: 5
                        Rectangle { width: 100; height: 30; radius: 15; color: "black"; border.color: "#333"
                            Row { anchors.centerIn: parent; spacing: 5
                                Text { text: "↻"; color: "#3498DB" }
                                Text { text: "Connected"; color: "#3498DB"; font.pixelSize: 12 }
                            }
                        }
                        Rectangle { width: 40; height: 20; radius: 4; color: "black"; anchors.right: parent.right
                            Text { text: "ROS"; color: "white"; font.pixelSize: 10; anchors.centerIn: parent }
                        }
                    }
                }

                // 3D Point Cloud
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#0A0E1A"
                    radius: 12
                    border.color: "#1A1F2E"
                    clip: true

                    PointCloudView {
                        id: pointCloudView
                        anchors.fill: parent
                        // usingLiveData / externalPoints set from RosBackend when topic arrives
                    }

                    Rectangle {
                        width: 140; height: 30; radius: 8; color: Qt.rgba(0,0,0,0.55)
                        anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 15
                        Text { text: "👁 3D Point Cloud"; color: "white"; font.pixelSize: 12; anchors.centerIn: parent }
                    }
                }

                // 2D Occupancy Map
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#080C14"
                    radius: 12
                    border.color: "#1A1F2E"
                    clip: true

                    OccupancyMapView {
                        id: occupancyMapView
                        anchors.fill: parent
                        usingLiveData: rosBackend.usingLiveData
                        gridData: rosBackend.gridData
                        gridWidth: rosBackend.gridWidth
                        gridHeight: rosBackend.gridHeight
                        robotX: rosBackend.robotX
                        robotY: rosBackend.robotY
                        robotAngle: rosBackend.robotAngle
                        mapMinX: rosBackend.mapMinX
                        mapMinY: rosBackend.mapMinY
                        mapMaxX: rosBackend.mapMaxX
                        mapMaxY: rosBackend.mapMaxY
                    }

                    Rectangle {
                        width: 140; height: 30; radius: 8; color: Qt.rgba(0,0,0,0.55)
                        anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 15
                        Text { text: "🗺 2D Occupancy Map"; color: "white"; font.pixelSize: 12; anchors.centerIn: parent }
                    }
                }

                // Telemetry Panel
                Rectangle {
                    id: telemetryPanel
                    Layout.preferredWidth: 350
                    Layout.fillHeight: true
                    color: "#1C1E24"
                    radius: 12

                    // ── BMS Live Data Properties ──────────────────────
                    property real bms_soc: 0
                    property real bms_current: 0
                    property real bms_voltage: 0
                    property real bms_power: 0
                    property real bms_temp: 0
                    property real bms_cell_avg: 0
                    property string bms_health: "--"
                    property string bms_lastUpdate: "--"
                    property bool bms_connected: false
                    property bool bms_fetching: false

                    // Animated display values (smooth interpolation)
                    property real disp_soc: 0
                    property real disp_current: 0
                    property real disp_voltage: 0
                    property real disp_power: 0
                    property real disp_temp: 0

                    Behavior on disp_soc     { NumberAnimation { duration: 800; easing.type: Easing.OutCubic } }
                    Behavior on disp_current { NumberAnimation { duration: 800; easing.type: Easing.OutCubic } }
                    Behavior on disp_voltage { NumberAnimation { duration: 800; easing.type: Easing.OutCubic } }
                    Behavior on disp_power   { NumberAnimation { duration: 800; easing.type: Easing.OutCubic } }
                    Behavior on disp_temp    { NumberAnimation { duration: 800; easing.type: Easing.OutCubic } }

                    // ── Polling Timer ─────────────────────────────────
                    Timer {
                        id: bmsTimer
                        interval: 1000
                        running: true
                        repeat: true
                        onTriggered: {
                            if (telemetryPanel.bms_fetching) return;
                            telemetryPanel.bms_fetching = true;
                            var xhr = new XMLHttpRequest();
                            xhr.open("GET", "http://192.168.1.186:5000/bms/summary", true);
                            xhr.timeout = 900;
                            xhr.onreadystatechange = function() {
                                if (xhr.readyState !== XMLHttpRequest.DONE) return;
                                telemetryPanel.bms_fetching = false;
                                if (xhr.status === 200) {
                                    try {
                                        var d = JSON.parse(xhr.responseText);
                                        telemetryPanel.bms_soc      = d.soc     || 0;
                                        telemetryPanel.bms_current  = d.current || 0;
                                        telemetryPanel.bms_voltage  = d.pack_voltage || 0;
                                        telemetryPanel.bms_power    = d.power   || 0;
                                        telemetryPanel.bms_temp     = (d.temperature_statistics && d.temperature_statistics.avg_temperature) || 0;
                                        telemetryPanel.bms_cell_avg = (d.cell_statistics && d.cell_statistics.avg_voltage) || 0;
                                        telemetryPanel.bms_health   = (d.battery_health && d.battery_health.overall_status) || "--";
                                        telemetryPanel.bms_connected = d.connected || false;
                                        // Animate to new values
                                        telemetryPanel.disp_soc     = telemetryPanel.bms_soc;
                                        telemetryPanel.disp_current = Math.abs(telemetryPanel.bms_current);
                                        telemetryPanel.disp_voltage = telemetryPanel.bms_voltage;
                                        telemetryPanel.disp_power   = Math.abs(telemetryPanel.bms_power);
                                        telemetryPanel.disp_temp    = telemetryPanel.bms_temp;
                                        
                                        // Update global backend for TopBar
                                        rosBackend.battery = Math.round(telemetryPanel.bms_soc);
                                        rosBackend.temperature = Math.round(telemetryPanel.bms_temp);

                                        var now = new Date();
                                        telemetryPanel.bms_lastUpdate = now.getHours() + ":" +
                                            (now.getMinutes()<10?"0":"") + now.getMinutes() + ":" +
                                            (now.getSeconds()<10?"0":"") + now.getSeconds();
                                    } catch(e) { telemetryPanel.bms_connected = false; }
                                } else {
                                    telemetryPanel.bms_connected = false;
                                }
                            };
                            xhr.send();
                        }
                    }

                    Column {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 14

                        // Header
                        RowLayout {
                            width: parent.width
                            Column {
                                Text { text: "Xavier-RGV-311"; color: "white"; font.pixelSize: 18; font.bold: true }
                                Text { text: "Katunayaka"; color: "#888"; font.pixelSize: 12 }
                            }
                            Item { Layout.fillWidth: true }
                            Rectangle { width: 40; height: 40; radius: 8; color: "#2A2D35"; Text { text: "🚚"; anchors.centerIn: parent; font.pixelSize: 20 } }
                        }

                        Rectangle { width: parent.width; height: 1; color: "#333" }

                        // Battery SOC + Current
                        TelemetryRow {
                            icon: "🔋"
                            label: "Battery Charge"
                            subLabel: "Current"
                            val1: telemetryPanel.disp_soc.toFixed(1) + "%"
                            val2: (telemetryPanel.bms_current < 0 ? "-" : "+") + telemetryPanel.disp_current.toFixed(1) + "A"
                            color1: telemetryPanel.disp_soc > 50 ? "#2ECC71" : (telemetryPanel.disp_soc > 20 ? "#F39C12" : "#E74C3C")
                            color2: "#888"
                        }
                        Rectangle { width: parent.width; height: 1; color: "#333" }

                        // Pack Voltage + Power
                        TelemetryRow {
                            icon: "⚡"
                            label: "Pack Voltage"
                            subLabel: "Power Draw"
                            val1: telemetryPanel.disp_voltage.toFixed(1) + "V"
                            val2: telemetryPanel.disp_power.toFixed(1) + "W"
                            color1: "#F4D03F"
                            color2: "#888"
                        }
                        Rectangle { width: parent.width; height: 1; color: "#333" }

                        // Temperature
                        TelemetryRow {
                            icon: "🌡"
                            label: "Battery Temp"
                            subLabel: "Avg cell voltage"
                            val1: telemetryPanel.disp_temp.toFixed(1) + "°C"
                            val2: telemetryPanel.bms_cell_avg.toFixed(3) + "V"
                            color1: telemetryPanel.disp_temp < 40 ? "#2ECC71" : (telemetryPanel.disp_temp < 55 ? "#F39C12" : "#E74C3C")
                            color2: "#888"
                        }
                        Rectangle { width: parent.width; height: 1; color: "#333" }

                        // Health status
                        TelemetryRow {
                            icon: "💊"
                            label: "Battery Health"
                            subLabel: ""
                            val1: telemetryPanel.bms_health.replace(/_/g, " ")
                            val2: ""
                            color1: telemetryPanel.bms_health === "good" ? "#2ECC71" : "#F39C12"
                            color2: "transparent"
                        }
                        Rectangle { width: parent.width; height: 1; color: "#333" }

                        // Speed row (static for now)
                        TelemetryRow { icon: "⏱"; label: "Speed"; subLabel: "Max Rec. Speed"; val1: "0.0 km/h"; val2: "N/A"; color1: "#2ECC71"; color2: "#888" }

                        Item { height: 4 }

                        // Footer: last update + connection dot
                        RowLayout {
                            width: parent.width
                            Text {
                                text: "BMS " + (telemetryPanel.bms_lastUpdate !== "--" ? "@ " + telemetryPanel.bms_lastUpdate : "connecting…")
                                color: "#666"; font.pixelSize: 10; Layout.alignment: Qt.AlignLeft
                            }
                            Row {
                                Layout.alignment: Qt.AlignRight
                                spacing: 4
                                Rectangle {
                                    width: 7; height: 7; radius: 4
                                    color: telemetryPanel.bms_connected ? "#2ECC71" : "#E74C3C"
                                    anchors.verticalCenter: parent.verticalCenter
                                    // Pulse animation when connected
                                    SequentialAnimation on opacity {
                                        running: telemetryPanel.bms_connected
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.3; duration: 600; easing.type: Easing.InOutSine }
                                        NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
                                    }
                                }
                                Text {
                                    text: telemetryPanel.bms_connected ? "BMS Live" : "BMS Offline"
                                    color: telemetryPanel.bms_connected ? "#2ECC71" : "#E74C3C"
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }
            }


            // -- MAIN TABS --
            Row {
                spacing: 20
                Repeater {
                    model: ["Defects (0)", "Unauthorized (0)", "PPE Violations (3)", "Environmental"]
                    Item {
                        width: tabText.width + 20
                        height: 40
                        Text {
                            id: tabText
                            anchors.centerIn: parent
                            text: modelData
                            color: index === dashboardRoot.currentTabIndex ? "white" : "#888"
                            font.pixelSize: 14
                            font.bold: index === dashboardRoot.currentTabIndex
                        }
                        Rectangle {
                            width: parent.width
                            height: 2
                            color: "#F4D03F"
                            anchors.bottom: parent.bottom
                            visible: index === dashboardRoot.currentTabIndex
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: dashboardRoot.currentTabIndex = index
                        }
                    }
                }
            }

            // -- CONTENT VIEWS --
            
            // PPE Violations View
            PpeViolationView {
                visible: dashboardRoot.currentTabIndex === 2
            }

            // -- ENVIRONMENTAL MONITORING SYSTEM BLOCK --
            Rectangle {
                visible: dashboardRoot.currentTabIndex === 3
                width: parent.width
                height: envColumn.height + 40
                color: "#1C1E24"
                radius: 12
                
                Column {
                    id: envColumn
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 20
                    spacing: 15

                    // Header
                    RowLayout {
                        width: parent.width
                        Column {
                            Text { text: "Environmental Monitoring System"; color: "white"; font.pixelSize: 20; font.bold: true }
                            Text { text: "Real-time air quality and environmental sensors"; color: "#888"; font.pixelSize: 12 }
                        }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 100
                            height: 30
                            radius: 15
                            color: "#0D2E1C"
                            border.color: "#2ECC71"
                            border.width: 1
                            Row {
                                anchors.centerIn: parent
                                spacing: 5
                                Rectangle { width: 8; height: 8; radius: 4; color: "#2ECC71" }
                                Text { text: "Connected"; color: "#2ECC71"; font.pixelSize: 12 }
                            }
                        }
                    }

                    // AQI and Temp/Humid
                    RowLayout {
                        width: parent.width
                        spacing: 20

                        // AQI Box
                        Rectangle {
                            width: 80
                            height: 80
                            radius: 12
                            color: "#2A2D35"
                            Column {
                                anchors.centerIn: parent
                                Text { text: "73"; color: "#F4D03F"; font.pixelSize: 24; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
                                Text { text: "AQI"; color: "#888"; font.pixelSize: 12; anchors.horizontalCenter: parent.horizontalCenter }
                            }
                            Rectangle {
                                width: 10
                                height: 10
                                radius: 5
                                color: "#F4D03F"
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: 5
                            }
                        }

                        Column {
                            Text { text: "Moderate"; color: "#F4D03F"; font.pixelSize: 18; font.bold: true }
                            Text { text: "Air quality is acceptable"; color: "#888"; font.pixelSize: 14 }
                        }

                        Item { Layout.fillWidth: true }

                        // Temperature
                        Rectangle {
                            width: 120
                            height: 60
                            radius: 8
                            color: "#2A2D35"
                            border.color: "#333"
                            Column {
                                anchors.centerIn: parent
                                Text { text: "🌡 Temperature"; color: "#888"; font.pixelSize: 12 }
                                Text { text: "29.8°C"; color: "#F4D03F"; font.pixelSize: 18; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
                            }
                        }

                        // Humidity
                        Rectangle {
                            width: 120
                            height: 60
                            radius: 8
                            color: "#2A2D35"
                            border.color: "#333"
                            Column {
                                anchors.centerIn: parent
                                Text { text: "💧 Humidity"; color: "#888"; font.pixelSize: 12 }
                                Text { text: "58.1%"; color: "#2ECC71"; font.pixelSize: 18; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
                            }
                        }
                    }

                    // Inner Tabs
                    Row {
                        spacing: 15
                        Rectangle { width: 100; height: 36; radius: 8; color: "#2A2D35"; Text { text: "Overview"; color: "white"; anchors.centerIn: parent } }
                        Rectangle { width: 140; height: 36; radius: 8; color: "#F4D03F"; Text { text: "⚠ Gas Detection"; color: "black"; font.bold: true; anchors.centerIn: parent } }
                        Rectangle { width: 120; height: 36; radius: 8; color: "#2A2D35"; Text { text: "💧 Particulates"; color: "white"; anchors.centerIn: parent } }
                    }

                    // 3 Graphs Row
                    RowLayout {
                        width: parent.width
                        spacing: 20

                        // Methane
                        GraphWidget {
                            Layout.fillWidth: true
                            title: "Methane"
                            baseValue: 18.8
                            unit: "ppm"
                            lineColor: "#F39C12"
                            statusText: "Safe"
                            statusColor: "#2ECC71"
                        }

                        // Carbon Monoxide
                        GraphWidget {
                            Layout.fillWidth: true
                            title: "Carbon Monoxide"
                            baseValue: 0.530
                            unit: "ppm"
                            lineColor: "#8E44AD"
                            statusText: "Safe"
                            statusColor: "#2ECC71"
                        }

                        // Nitrogen Dioxide
                        GraphWidget {
                            Layout.fillWidth: true
                            title: "Nitrogen Dioxide"
                            baseValue: 0.040
                            unit: "ppm"
                            lineColor: "#9B59B6"
                            statusText: "Safe"
                            statusColor: "#2ECC71"
                        }
                    }
                }
            }
        }
    }
}
