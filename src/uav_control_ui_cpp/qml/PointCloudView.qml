import QtQuick 2.15

// ──────────────────────────────────────────────────────────────────────────────
// PointCloudView.qml
// Renders a 3D point cloud using a perspective-projection Canvas.
//
// To inject live ROS data:
//   pointCloudView.externalPoints = [{x, y, z}, ...]
//   pointCloudView.usingLiveData  = true
// ──────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: pcView
    color: "#0A0E1A"
    radius: 0
    clip: true

    // ── Public API for ROS integration ──────────────────────────────────────
    property bool usingLiveData: false
    property var  externalPoints: []   // [{x,y,z}, ...]

    // ── Internal state ───────────────────────────────────────────────────────
    property real rotY: 0.4
    property var  samplePoints: []

    // ── Sample data generator ────────────────────────────────────────────────
    function generateWarehousePoints() {
        var pts = []
        var W = 12, H = 5, D = 12

        // Floor
        for (var f = 0; f < 250; f++)
            pts.push({ x:(Math.random()-0.5)*W, y:0,             z:(Math.random()-0.5)*D, c:0.20+Math.random()*0.2, isRobot:false })

        // Ceiling (sparse)
        for (var c2 = 0; c2 < 60; c2++)
            pts.push({ x:(Math.random()-0.5)*W, y:H,             z:(Math.random()-0.5)*D, c:0.15,                  isRobot:false })

        // 4 Walls
        for (var w = 0; w < 80; w++) {
            pts.push({ x:(Math.random()-0.5)*W, y:Math.random()*H, z: D/2, c:0.55, isRobot:false })
            pts.push({ x:(Math.random()-0.5)*W, y:Math.random()*H, z:-D/2, c:0.55, isRobot:false })
            pts.push({ x: W/2, y:Math.random()*H, z:(Math.random()-0.5)*D, c:0.50, isRobot:false })
            pts.push({ x:-W/2, y:Math.random()*H, z:(Math.random()-0.5)*D, c:0.50, isRobot:false })
        }

        // Shelving / equipment clusters
        var clusters = [
            {bx:-4, bz:-4, bh:3.0}, {bx: 3, bz:-3, bh:2.5},
            {bx:-2, bz: 3, bh:2.0}, {bx: 4, bz: 4, bh:3.5},
            {bx: 0, bz:-4, bh:1.5}, {bx:-4, bz: 1, bh:2.8}
        ]
        for (var cl = 0; cl < clusters.length; cl++) {
            var ck = clusters[cl]
            for (var m = 0; m < 70; m++)
                pts.push({
                    x: ck.bx + (Math.random()-0.5)*1.6,
                    y: Math.random()*ck.bh,
                    z: ck.bz + (Math.random()-0.5)*1.6,
                    c: 0.8 + Math.random()*0.2, isRobot:false
                })
        }

        // Robot body (centre)
        for (var r = 0; r < 50; r++) {
            var a = Math.random()*Math.PI*2
            var rad = Math.random()*0.55
            pts.push({ x:Math.cos(a)*rad, y:0.25+Math.random()*0.8, z:Math.sin(a)*rad*0.6, c:1.0, isRobot:true })
        }

        return pts
    }

    // ── Height → colour (blue→cyan→green→yellow→red) ────────────────────────
    function heightColor(y, minY, maxY) {
        var t = Math.max(0, Math.min(1, (y - minY) / (maxY - minY)))
        var r, g, b
        if      (t < 0.25) { r=0;   g=Math.round(t*4*255);       b=255 }
        else if (t < 0.50) { r=0;   g=255;                        b=Math.round((1-(t-0.25)*4)*255) }
        else if (t < 0.75) { r=Math.round((t-0.5)*4*255);         g=255; b=0 }
        else               { r=255; g=Math.round((1-(t-0.75)*4)*255); b=0 }
        return "rgb("+r+","+g+","+b+")"
    }

    // ── Project world → screen ───────────────────────────────────────────────
    function projectPt(x, y, z, cosR, sinR, cx, cy, fov) {
        var rx = x * cosR + z * sinR
        var rz = -x * sinR + z * cosR + 7
        if (rz <= 0.3) return null
        var sc = fov / rz
        return { sx: cx + rx*sc, sy: cy - (y-3.2)*sc, depth: rz, sc:sc }
    }

    Component.onCompleted: { samplePoints = generateWarehousePoints() }

    // ── Canvas ───────────────────────────────────────────────────────────────
    Canvas {
        id: canvas
        anchors.fill: parent

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            // Background gradient
            var bg = ctx.createLinearGradient(0, 0, 0, height)
            bg.addColorStop(0, "#0D1117")
            bg.addColorStop(1, "#080C14")
            ctx.fillStyle = bg
            ctx.fillRect(0, 0, width, height)

            var cx  = width  * 0.5
            var cy  = height * 0.58
            var fov = Math.min(width, height) * 0.55
            var cosR = Math.cos(pcView.rotY)
            var sinR = Math.sin(pcView.rotY)

            var pts = pcView.usingLiveData ? pcView.externalPoints : pcView.samplePoints
            if (!pts || pts.length === 0) return

            // Project
            var projected = []
            for (var i = 0; i < pts.length; i++) {
                var p = pts[i]
                var pp = projectPt(p.x, p.y, p.z, cosR, sinR, cx, cy, fov)
                if (pp) {
                    pp.y    = p.y
                    pp.isRobot = p.isRobot || false
                    projected.push(pp)
                }
            }
            // Sort back→front
            projected.sort(function(a,b){ return b.depth - a.depth })

            // Floor grid
            ctx.strokeStyle = "rgba(0,180,255,0.12)"
            ctx.lineWidth = 0.6
            for (var g = -6; g <= 6; g++) {
                var a = projectPt(g, 0, -6, cosR, sinR, cx, cy, fov)
                var b2 = projectPt(g, 0,  6, cosR, sinR, cx, cy, fov)
                if (a && b2) { ctx.beginPath(); ctx.moveTo(a.sx,a.sy); ctx.lineTo(b2.sx,b2.sy); ctx.stroke() }
                var c3 = projectPt(-6, 0, g, cosR, sinR, cx, cy, fov)
                var d  = projectPt( 6, 0, g, cosR, sinR, cx, cy, fov)
                if (c3 && d) { ctx.beginPath(); ctx.moveTo(c3.sx,c3.sy); ctx.lineTo(d.sx,d.sy); ctx.stroke() }
            }

            // Draw points
            for (var j = 0; j < projected.length; j++) {
                var pj = projected[j]
                var ds = Math.max(1, Math.min(3.5, pj.sc * 0.09))
                ctx.globalAlpha = pj.isRobot ? 1.0 : 0.82
                ctx.fillStyle   = pj.isRobot ? "#FF5722" : heightColor(pj.y, 0, 5)
                ctx.fillRect(pj.sx - ds*0.5, pj.sy - ds*0.5, ds, ds)
            }
            ctx.globalAlpha = 1.0

            // Colour legend bar
            var barH = height * 0.55
            var barX = width - 22
            var barY = (height - barH) * 0.5
            var grad = ctx.createLinearGradient(0, barY+barH, 0, barY)
            grad.addColorStop(0.00, "#0000FF")
            grad.addColorStop(0.25, "#00FFFF")
            grad.addColorStop(0.50, "#00FF00")
            grad.addColorStop(0.75, "#FFFF00")
            grad.addColorStop(1.00, "#FF0000")
            ctx.fillStyle = grad
            ctx.fillRect(barX, barY, 8, barH)
            ctx.strokeStyle = "#444"; ctx.lineWidth = 0.5
            ctx.strokeRect(barX, barY, 8, barH)
            ctx.fillStyle = "#888"; ctx.font = "9px sans-serif"
            ctx.fillText("High", barX - 1, barY + 10)
            ctx.fillText("Low",  barX - 1, barY + barH + 2)
        }
    }

    // ── Rotation animation ───────────────────────────────────────────────────
    Timer {
        interval: 40    // 25 fps
        running:  true
        repeat:   true
        onTriggered: { pcView.rotY += 0.006; canvas.requestPaint() }
    }

    // ── Data source badge ────────────────────────────────────────────────────
    Row {
        anchors.bottom: parent.bottom; anchors.right: parent.right
        anchors.margins: 10; spacing: 5
        Rectangle {
            width: 7; height: 7; radius: 4
            color: pcView.usingLiveData ? "#2ECC71" : "#F39C12"
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: pcView.usingLiveData ? "Live /pointcloud2" : "Sample Data"
            color: "#888"; font.pixelSize: 10
        }
    }
}
