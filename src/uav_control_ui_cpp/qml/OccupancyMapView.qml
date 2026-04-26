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

    // ── Canvas ────────────────────────────────────────────────────────────────
    Canvas {
        id: canvas
        anchors.fill: parent

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            // Background
            ctx.fillStyle = "#080C14"
            ctx.fillRect(0, 0, width, height)

            var W = mapView.gridWidth
            var H = mapView.gridHeight
            var data = mapView.usingLiveData ? mapView.gridData : mapView.sampleGrid
            if (!data || data.length === 0) return

            // Cell size to fit canvas
            var cs = Math.min(width / W, height / H)
            var offX = (width  - cs * W) * 0.5
            var offY = (height - cs * H) * 0.5

            // Draw grid cells
            for (var ry = 0; ry < H; ry++) {
                for (var rx = 0; rx < W; rx++) {
                    var v = data[ry * W + rx]
                    if      (v === -1)  ctx.fillStyle = "#1A1D26"   // unknown — dark grey
                    else if (v === 0)   ctx.fillStyle = "#D8DCE8"   // free — light
                    else if (v === 100) ctx.fillStyle = "#1C2030"   // occupied — near black
                    else {
                        // Partial occupancy (0-100)
                        var t = v / 100.0
                        var grey = Math.round(220 - t * 200)
                        ctx.fillStyle = "rgb(" + grey + "," + grey + "," + grey + ")"
                    }
                    ctx.fillRect(offX + rx*cs, offY + ry*cs, cs + 0.5, cs + 0.5)
                }
            }

            // Grid overlay (only if cells are large enough)
            if (cs >= 5) {
                ctx.strokeStyle = "rgba(0,0,0,0.12)"
                ctx.lineWidth = 0.3
                for (var gx = 0; gx <= W; gx++) {
                    ctx.beginPath()
                    ctx.moveTo(offX + gx*cs, offY)
                    ctx.lineTo(offX + gx*cs, offY + H*cs)
                    ctx.stroke()
                }
                for (var gy = 0; gy <= H; gy++) {
                    ctx.beginPath()
                    ctx.moveTo(offX,        offY + gy*cs)
                    ctx.lineTo(offX + W*cs, offY + gy*cs)
                    ctx.stroke()
                }
            }

            // ── Robot marker ─────────────────────────────────────────────────
            var rx2  = mapView.usingLiveData ? mapView.robotX : mapView._rx
            var ry2  = mapView.usingLiveData ? mapView.robotY : mapView._ry
            var ra   = mapView.usingLiveData ? mapView.robotAngle : mapView._ra
            var rsx  = offX + rx2 * cs
            var rsy  = offY + ry2 * cs
            var rr   = cs * 2.2

            // Shadow
            ctx.beginPath()
            ctx.arc(rsx, rsy, rr + 2, 0, Math.PI*2)
            ctx.fillStyle = "rgba(0,0,0,0.4)"
            ctx.fill()

            // Body
            ctx.beginPath()
            ctx.arc(rsx, rsy, rr, 0, Math.PI*2)
            ctx.fillStyle = "#E74C3C"
            ctx.fill()
            ctx.strokeStyle = "#FF8C00"; ctx.lineWidth = 1.5
            ctx.stroke()

            // Heading arrow
            ctx.beginPath()
            ctx.moveTo(rsx, rsy)
            ctx.lineTo(rsx + Math.cos(ra)*rr*1.7, rsy + Math.sin(ra)*rr*1.7)
            ctx.strokeStyle = "white"; ctx.lineWidth = 1.5
            ctx.stroke()

            // Scan circle
            ctx.beginPath()
            ctx.arc(rsx, rsy, rr * 4.5, 0, Math.PI*2)
            ctx.strokeStyle = "rgba(231,76,60,0.18)"; ctx.lineWidth = 0.8
            ctx.stroke()
        }
    }

    // ── Wander timer (sample mode only) ──────────────────────────────────────
    Timer {
        interval: 80
        running:  true
        repeat:   true
        onTriggered: {
            if (mapView.usingLiveData) return

            mapView._rx += mapView._rvx
            mapView._ry += mapView._rvy

            // Bounce off grid bounds and avoid walls
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

            canvas.requestPaint()
        }
    }

    // ── Data source badge ─────────────────────────────────────────────────────
    Row {
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
}
