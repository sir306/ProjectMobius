import QtQuick
import QtQuick.Controls

Item {
    id: root
    // re-declare for the delegate:
    property real pointValueX
    property real pointValueY
    property bool hovered: false

    width: 10; height: 10

    // keep interactions alive even when “invisible”
    opacity: hovered ? 1.0 : 0.0

    // If we want to bring back the scatter point visibility toggle use this bellow
    // {
    //     var showAll = chartSettings.showAllPoints;
    //     if (showAll) {
    //         return 1.0;
    //     }

    //     var normalizedY = (pointValueY - agentGraph.yOffset) / agentGraph.majorYStep;
    //     var roundedY = Math.round(normalizedY);
    //     var diff = Math.abs(normalizedY - roundedY);
    //     return (diff < 1e-6) ? 1.0 : 0.0;
    // }

    Rectangle {
        anchors.fill: parent
        radius: width/2
        color: hovered ? "yellow" : "green"
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered: hovered = true
        onExited:  hovered = false

        ToolTip.visible: hovered
        ToolTip.text:
            "X=" + pointValueX.toFixed(2) +
            "\nY=" + pointValueY.toFixed(0)
    }
}
