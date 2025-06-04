import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtGraphs
import PlotUE_Data 1.0

Window {
    id: window
    width: 640; height: 480
    visible: true
    title: qsTr("Chart")
    color: "black"
    flags: Qt.Window
         | Qt.WindowTitleHint
         | Qt.WindowMinMaxButtonsHint
         | Qt.WindowCloseButtonHint

    // ────────────────────────────────────────────
    // HEADER: ColumnLayout with a single Item to host anchors
    // ────────────────────────────────────────────
    ColumnLayout {
        id: headerLayout
        anchors {
            top:    parent.top
            left:   parent.left
            right:  parent.right
        }
        spacing: 10
        //padding: 10

        // 1) Connection status
        Text {
            text: wsMgr.connectionStatus
            font.pixelSize: 16
            color: wsMgr.connectionStatus === "Connected" ? "lightgreen" : "tomato"
            Layout.alignment: Qt.AlignHCenter
        }

        // 2) Chart title
        Label {
            text: chartSettings.title
            font.pixelSize: 24
            color: "white"
            Layout.alignment: Qt.AlignHCenter
        }

        // 3) Static + dynamic time/count display
        Item {
            Layout.fillWidth: true
            height: 60

            // — static label —
            Text {
                id: labelText
                text: "Current Simulation Time:\nCurrent Occupancy Count:"
                font.pixelSize: 14
                color: "white"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignRight
                renderType: Text.NativeRendering
                layer.enabled: true; layer.smooth: true

                anchors.horizontalCenter: parent.horizontalCenter
                width: implicitWidth
            }

            // — dynamic values —
            Text {
                id: valueText
                font.pixelSize: 14
                color: "white"
                wrapMode: Text.WordWrap
                renderType: Text.NativeRendering
                layer.enabled: true; layer.smooth: true

                anchors.verticalCenter: labelText.verticalCenter
                anchors.left:           labelText.right; anchors.leftMargin: 12

                text: chartSettings.statusDisplay
            }
        }
    }

    // ────────────────────────────────────────────
    // CHART: GraphsView anchored below header
    // ────────────────────────────────────────────
    Item {
        id: chartArea
        anchors {
            top:    headerLayout.bottom
            left:   parent.left
            right:  parent.right
            bottom: parent.bottom
        }

        GraphsView {
            id: agentGraph
            anchors.fill: parent

            axisX: ValueAxis {
                id: xAxis
                min:           axisSettings.xMin
                max:           axisSettings.xChartMax
                titleText:     axisSettings.xTitle
                gridVisible:   axisSettings.xGridVisible
            }
            axisY: ValueAxis {
                id: yAxis
                min:           axisSettings.yMin
                titleText:     axisSettings.yTitle
                gridVisible:   axisSettings.yGridVisible

                property real yRange: axisSettings.yMax - axisSettings.yMin
                property real step:   Math.pow(10, Math.floor(Math.log10(yRange / 5)))
                max: Math.ceil(axisSettings.yMax / step) * step
            }

            LineSeries    { id: lineSeries;    color: "red" }
            ScatterSeries {
                id: scatterSeries
                pointDelegate: ScatterSeriesHoverItem { }
            }

            XYModelMapper { model: chartModel; series: lineSeries;    xSection: 0; ySection: 1 }
            XYModelMapper { model: chartModel; series: scatterSeries; xSection: 0; ySection: 1 }
        }

        // update plot metrics when area or axis changes
        Connections {
            target: agentGraph
            onPlotAreaChanged: {
                chartSettings.setPlotMetrics(
                    agentGraph.plotArea.x,
                    agentGraph.plotArea.width,
                    xAxis.min,
                    xAxis.max
                )
            }
        }
        Connections {
            target: xAxis
            onMinChanged: chartSettings.setPlotMetrics(
                agentGraph.plotArea.x,
                agentGraph.plotArea.width,
                xAxis.min,
                xAxis.max
            )
            onMaxChanged: chartSettings.setPlotMetrics(
                agentGraph.plotArea.x,
                agentGraph.plotArea.width,
                xAxis.min,
                xAxis.max
            )
        }

        // BLUE playbar stripe
        Rectangle {
            id: currentTimeLine
            z:      1
            width:  2
            height: agentGraph.plotArea.height
            y:      agentGraph.plotArea.y
            color:  "blue"
            x:      chartSettings.playbarX
        }
    }
}
