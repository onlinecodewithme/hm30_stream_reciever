import QtQuick 2.15

// ──────────────────────────────────────────────────────────────────────────────
// OccupancyMapView.qml
// Renders a 2D occupancy grid on a QML Canvas.
//
// To inject live ROS nav_msgs/OccupancyGrid data:
//   mapView.gridWidth  = msg.info.width
//   mapView.gridHeight = msg.info.height
//   mapView.gridData   = msg.data       // flat Int8Array / JS array
//   mapView.robotX     = robot_grid_col
//   mapView.robotY     = robot_grid_row
//   mapView.robotAngle = heading_radians
//   mapView.usingLiveData = true
// ──────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: mapView
    color: "#0A0D14"
    radius: 0
    clip: true

    // ── Public API for ROS integration ───────────────────────────────────────
    property bool usingLiveData: false
    property var  gridData:    []    // flat array: -1=unknown, 0=free, 100=occupied
    property int  gridWidth:   80
    property int  gridHeight:  60
    property real robotX:      40   // grid column
    property real robotY:      30   // grid row
    property real robotAngle:  0.0  // radians
    property int  mapMinX: 0
    property int  mapMinY: 0
    property int  mapMaxX: 80
    property int  mapMaxY: 60

    // ── Interaction & Smoothing ──────────────────────────────────────────────
    property real zoomScale: 1.0
    property real panOffsetX: 0.0
    property real panOffsetY: 0.0

    property real sRobotX: usingLiveData ? robotX : _rx
    Behavior on sRobotX { NumberAnimation { duration: 150 } }
    
    property real sRobotY: usingLiveData ? robotY : _ry
    Behavior on sRobotY { NumberAnimation { duration: 150 } }
    
    property real sRobotAngle: usingLiveData ? robotAngle : _ra
    Behavior on sRobotAngle { NumberAnimation { duration: 150 } }

    // ── Internal: animated robot wander for sample mode ──────────────────────
    property real _rx: 40
    property real _ry: 30
    property real _ra: 0.0
    property real _rvx: 0.12
    property real _rvy: 0.07

    // ── Sample floor plan (encoded as room rectangles) ────────────────────────
    // Grid: 0=free, 100=wall, -1=outside
    property var sampleGrid: []

    function buildSampleGrid() {
        var W = gridWidth, H = gridHeight
        var g = []
        // Fill everything as unknown/outside
        for (var i = 0; i < W * H; i++) g.push(-1)

        function cell(x, y) { return y * W + x }

        function fillRect(x1, y1, x2, y2, val) {
            for (var ry = y1; ry <= y2; ry++)
                for (var rx = x1; rx <= x2; rx++)
                    if (rx >= 0 && rx < W && ry >= 0 && ry < H)
                        g[cell(rx, ry)] = val
        }

        function wallRect(x1, y1, x2, y2) {
            // Fill interior free, border wall
            fillRect(x1, y1, x2, y2, 0)
            for (var ry2 = y1; ry2 <= y2; ry2++)
                for (var rx2 = x1; rx2 <= x2; rx2++)
                    if (rx2 === x1 || rx2 === x2 || ry2 === y1 || ry2 === y2)
                        g[cell(rx2, ry2)] = 100
        }

        // ── Room layout ─────────────────────────────────────────────────────
        // Outer boundary
        wallRect(2, 2, W-3, H-3)

        // Room 1 — top-left large room
        wallRect(3, 3, 28, 22)
        // Door opening top-left room → corridor: clear bottom wall segment
        for (var d1 = 13; d1 <= 16; d1++) g[cell(d1, 22)] = 0

        // Room 2 — top-right
        wallRect(32, 3, W-4, 22)
        for (var d2 = 45; d2 <= 48; d2++) g[cell(d2, 22)] = 0

        // Room 3 — bottom-left
        wallRect(3, 26, 22, H-4)
        for (var d3 = 26; d3 <= 29; d3++) g[cell(22, d3)] = 0

        // Room 4 — bottom-centre
        wallRect(26, 26, 50, H-4)
        for (var d4 = 34; d4 <= 37; d4++) g[cell(26, d4)] = 0

        // Room 5 — bottom-right small
        wallRect(54, 26, W-4, H-4)

        // Horizontal corridor
        fillRect(3, 23, W-4, 25, 0)

        // Vertical corridor left
        fillRect(29, 3, 31, H-4, 0)

        // Small alcove top-centre
        wallRect(32, 3, 42, 12)
        for (var d5 = 36; d5 <= 38; d5++) g[cell(d5, 12)] = 0

        // Objects / furniture in rooms (small walls)
        fillRect(6,  6, 10, 8, 100)   // desk top-left room
        fillRect(6, 12, 10, 14, 100)  // desk
        fillRect(18, 6, 22, 8, 100)   // shelf
        fillRect(35, 5, 40, 9, 100)   // equipment top-right
        fillRect(55, 5, 60, 10, 100)  // shelf
        fillRect(5, 30, 9, 34, 100)   // box bottom-left
        fillRect(14, 30, 18, 35, 100) // box

        return g
    }

    Component.onCompleted: {
        sampleGrid = buildSampleGrid()
        canvas.requestPaint()
    }

    // ── Interaction ───────────────────────────────────────────────────────────
    MouseArea {
        anchors.fill: parent
        property real lastX: 0
        property real lastY: 0
        
        onWheel: {
            var zoomFactor = 1.1;
            if (wheel.angleDelta.y > 0) mapView.zoomScale *= zoomFactor;
            else if (wheel.angleDelta.y < 0) mapView.zoomScale /= zoomFactor;
            
            if (mapView.zoomScale < 0.1) mapView.zoomScale = 0.1;
            if (mapView.zoomScale > 10.0) mapView.zoomScale = 10.0;
        }
        
        onPressed: {
            lastX = mouse.x;
            lastY = mouse.y;
        }
        
        onPositionChanged: {
            mapView.panOffsetX += (mouse.x - lastX);
            mapView.panOffsetY += (mouse.y - lastY);
            lastX = mouse.x;
            lastY = mouse.y;
        }
    }

    // ── Canvas ────────────────────────────────────────────────────────────────
    Canvas {
        id: canvas
        anchors.fill: parent

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset() // Reset transform matrix
            ctx.clearRect(0, 0, width, height)

            // Background
            ctx.fillStyle = "#080C14"
            ctx.fillRect(0, 0, width, height)

            var W = mapView.gridWidth
            var H = mapView.gridHeight
            var data = mapView.usingLiveData ? mapView.gridData : mapView.sampleGrid
            if (!data || data.length === 0) return

            // Cell size to fit canvas based on known bounds
            var effMinX = mapView.usingLiveData ? mapView.mapMinX : 0;
            var effMinY = mapView.usingLiveData ? mapView.mapMinY : 0;
            var effMaxX = mapView.usingLiveData ? mapView.mapMaxX : W - 1;
            var effMaxY = mapView.usingLiveData ? mapView.mapMaxY : H - 1;
            
            // Fallback if empty
            if (effMaxX < effMinX || effMaxY < effMinY) {
                effMinX = 0; effMinY = 0; effMaxX = W - 1; effMaxY = H - 1;
            }
            
            // Add padding
            effMinX = Math.max(0, effMinX - 5);
            effMinY = Math.max(0, effMinY - 5);
            effMaxX = Math.min(W - 1, effMaxX + 5);
            effMaxY = Math.min(H - 1, effMaxY + 5);
            
            var viewW = effMaxX - effMinX + 1;
            var viewH = effMaxY - effMinY + 1;

            var baseCs = Math.min(width / viewW, height / viewH)
            
            // Ego-Centric Transformation
            var cx = width / 2;
            var cy = height / 2;
            
            ctx.save()
            // 1. Move to screen center
            ctx.translate(cx + mapView.panOffsetX, cy + mapView.panOffsetY)
            // 2. Scale (with horizontal mirror to correct map orientation)
            ctx.scale(-mapView.zoomScale, mapView.zoomScale)
            // 3. Rotate so robot faces UP (0 is right, -PI/2 is up)
            ctx.rotate(-mapView.sRobotAngle - Math.PI/2)
            // 4. Translate map so robot is at 0,0
            ctx.translate(-mapView.sRobotX * baseCs, -mapView.sRobotY * baseCs)

            // Draw grid cells within the view
            for (var ry = effMinY; ry <= effMaxY; ry++) {
                for (var rx = effMinX; rx <= effMaxX; rx++) {
                    var v = data[ry * W + rx]
                    if      (v === -1)  continue;   // skip drawing unknown space
                    else if (v === 0)   ctx.fillStyle = "#D8DCE8"   // free — light
                    else if (v === 100) ctx.fillStyle = "#1C2030"   // occupied — near black
                    else {
                        var t = v / 100.0
                        var grey = Math.round(220 - t * 200)
                        ctx.fillStyle = "rgb(" + grey + "," + grey + "," + grey + ")"
                    }
                    var drawX = rx * baseCs;
                    var drawY = ry * baseCs;
                    ctx.fillRect(drawX, drawY, baseCs + 0.5, baseCs + 0.5)
                }
            }

            // ── Robot marker ─────────────────────────────────────────────────
            // In this transformed space, the robot is always drawn at its grid coordinate.
            var rCanvasX = mapView.sRobotX * baseCs;
            var rCanvasY = mapView.sRobotY * baseCs;
            var rr = baseCs * 2.2;

            // Shadow
            ctx.beginPath()
            ctx.arc(rCanvasX, rCanvasY, rr + 2, 0, Math.PI*2)
            ctx.fillStyle = "rgba(0,0,0,0.4)"
            ctx.fill()

            // Body
            ctx.beginPath()
            ctx.arc(rCanvasX, rCanvasY, rr, 0, Math.PI*2)
            ctx.fillStyle = "#E74C3C"
            ctx.fill()
            ctx.strokeStyle = "#FF8C00"; ctx.lineWidth = 1.5
            ctx.stroke()

            // Heading arrow
            ctx.beginPath()
            ctx.moveTo(rCanvasX, rCanvasY)
            ctx.lineTo(rCanvasX + Math.cos(mapView.sRobotAngle)*rr*1.7, rCanvasY + Math.sin(mapView.sRobotAngle)*rr*1.7)
            ctx.strokeStyle = "white"; ctx.lineWidth = 1.5
            ctx.stroke()

            // Scan circle
            ctx.beginPath()
            ctx.arc(rCanvasX, rCanvasY, rr * 4.5, 0, Math.PI*2)
            ctx.strokeStyle = "rgba(231,76,60,0.18)"; ctx.lineWidth = 0.8
            ctx.stroke()
            
            ctx.restore()
        }
    }

    // ── Wander timer (sample mode + 30fps UI loop) ───────────────────────────
    Timer {
        interval: 33
        running:  true
        repeat:   true
        onTriggered: {
            if (!mapView.usingLiveData) {
                mapView._rx += mapView._rvx
                mapView._ry += mapView._rvy

                // Bounce off grid bounds
                if (mapView._rx < 5  || mapView._rx > mapView.gridWidth  - 5) mapView._rvx *= -1
                if (mapView._ry < 5  || mapView._ry > mapView.gridHeight - 5) mapView._rvy *= -1

                // Slightly vary angle from velocity
                mapView._ra = Math.atan2(mapView._rvy, mapView._rvx)

                // Slow wander perturbation
                mapView._rvx += (Math.random()-0.5)*0.015
                mapView._rvy += (Math.random()-0.5)*0.015
                // Clamp speed
                var sp = Math.sqrt(mapView._rvx*mapView._rvx + mapView._rvy*mapView._rvy)
                if (sp > 0.25) { mapView._rvx /= sp; mapView._rvy /= sp; mapView._rvx *= 0.25; mapView._rvy *= 0.25 }
            }
            // Always repaint (live updates need this too)
            canvas.requestPaint()
        }
    }

    // ── Data source badge & Recenter Button ──────────────────────────────────
    Row {
        id: badgeRow
        anchors.bottom: parent.bottom; anchors.right: parent.right
        anchors.margins: 10; spacing: 5
        Rectangle {
            width: 7; height: 7; radius: 4
            color: mapView.usingLiveData ? "#2ECC71" : "#F39C12"
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: mapView.usingLiveData ? "Live /map" : "Sample Data"
            color: "#888"; font.pixelSize: 10
        }
    }

    Rectangle {
        width: 80; height: 26; radius: 13
        anchors.bottom: badgeRow.top; anchors.right: parent.right
        anchors.margins: 10
        color: "#252B3B"
        border.color: "#3A4055"
        visible: mapView.panOffsetX !== 0 || mapView.panOffsetY !== 0 || mapView.zoomScale !== 1.0
        
        Text { anchors.centerIn: parent; text: "Recenter"; color: "white"; font.pixelSize: 12 }
        
        MouseArea {
            anchors.fill: parent
            onClicked: {
                mapView.panOffsetX = 0
                mapView.panOffsetY = 0
                mapView.zoomScale = 1.0
            }
        }
    }
}
