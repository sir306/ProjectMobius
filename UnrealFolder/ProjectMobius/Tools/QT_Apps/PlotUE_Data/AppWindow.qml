import QtQuick
import QtQuick.Controls
import QtGraphs
import PlotUE_Data 1.0

Window {
    id: window
    width: 640; height: 480
    visible: true
    title: qsTr("Chart")
    color: "black"
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint

    // ────────────────────────────────────────────
    // HEADER
    // ────────────────────────────────────────────
    Column {
        id: header
        anchors { top: parent.top; left: parent.left; right: parent.right }
        spacing: 10
        padding: 10

        Label {
            text: chartSettings.title
            font.pixelSize: 24
            color: "white"
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    // ────────────────────────────────────────────
    // CHART AREA (white background)
    // ────────────────────────────────────────────
    Rectangle {
        id: chartArea
        color: "white"
        clip: false
        anchors {
            top:    header.bottom
            left:   parent.left
            right:  parent.right
            bottom: parent.bottom
        }

        // ────────────────────────────────────────
        // 1) GraphsView
        // ────────────────────────────────────────
        GraphsView {
            id: agentGraph

            anchors {
                top:    parent.top
                bottom: parent.bottom
                left:   parent.left
                right:  parent.right

                // Space for tick labels and Y-axis title
                leftMargin:   40
                rightMargin:  20
                topMargin:    20
                bottomMargin: 40
            }

            property real px: 1.0 / window.screen.devicePixelRatio
            antialiasing: false

            marginBottom: 0
            marginLeft:   0

            theme: GraphsTheme {
                theme: GraphsTheme.Theme.UserDefined
                backgroundVisible: true
                backgroundColor: "white"
                plotAreaBackgroundVisible: true
                plotAreaBackgroundColor: "white"

                axisX.labelTextColor: "black"
                axisY.labelTextColor: "black"
                axisX.mainColor: "black"
                axisY.mainColor: "black"
                axisX.subColor:  "black"
                axisY.subColor:  "black"

                axisX.mainWidth: agentGraph.px
                axisY.mainWidth: agentGraph.px
                axisX.subWidth:  agentGraph.px
                axisY.subWidth:  agentGraph.px

                gridVisible: true
                grid.mainColor: "black"
                grid.subColor:  "black"
                grid.mainWidth: agentGraph.px
                grid.subWidth:  0
            }

            axisX: ValueAxis {
                id: xAxis
                min: axisSettings.xMin
                max: axisSettings.xChartMax
                gridVisible: axisSettings.xGridVisible
                titleColor: "black"
                subTickCount: 0
            }

            axisY: ValueAxis {
                id: yAxis
                min: axisSettings.yMin
                max: axisSettings.yChartMax
                gridVisible: axisSettings.yGridVisible
                titleColor: "black"
                subTickCount: 0
            }

            LineSeries    { id: lineSeries;    color: "dark green" }
            ScatterSeries {
                id: scatterSeries
                pointDelegate: GraphHoverItem { }
            }

            XYModelMapper { model: chartModel; series: lineSeries;    xSection:0; ySection:1 }
            XYModelMapper { model: chartModel; series: scatterSeries; xSection:0; ySection:1 }
        }

        // ────────────────────────────────────────
        // Y-axis title: fixed gap from axis, robust to text length
        // ────────────────────────────────────────
        Text {
            id: yAxisTitle
            text: axisSettings.yTitle
            color: "black"
            rotation: -90
            transformOrigin: Item.Center
            font.pixelSize: 14

            // Vertical center of the plot area
            property real plotCenterY: agentGraph.y
                                       + agentGraph.plotArea.y
                                       + agentGraph.plotArea.height / 2

            // Visual right edge should be a fixed distance from plot's left edge
            property real plotLeft: agentGraph.x + agentGraph.plotArea.x
            property real visualRightEdge: plotLeft - 50

            // After -90° rotation around center:
            //   visual right edge = x + width/2 + height/2
            // Solving for x:
            //   x = visualRightEdge - width/2 - height/2
            x: visualRightEdge - width/2 - height/2
            y: plotCenterY - height/2
        }


        // ────────────────────────────────────────
        // X-axis title: top-aligned, fixed gap
        // ────────────────────────────────────────
        Text {
            id: xAxisTitle
            text: axisSettings.xTitle
            color: "black"
            font.pixelSize: 14

            // centered under the graph itself
            anchors.horizontalCenter: agentGraph.horizontalCenter

            // fixed gap below the X axis / tick labels
            anchors.top: agentGraph.bottom
            anchors.topMargin: 8        // same gap as Y → visually balanced
        }

        // ────────────────────────────────────────
        // Plot metrics → C++
        // ────────────────────────────────────────
        Connections {
            target: agentGraph
            onPlotAreaChanged: chartSettings.setPlotMetrics(
                                   agentGraph.x + agentGraph.plotArea.x,
                                   agentGraph.plotArea.width,
                                   xAxis.min,
                                   xAxis.max
                               )
        }

        Connections {
            target: xAxis
            onMinChanged: chartSettings.setPlotMetrics(
                              agentGraph.x + agentGraph.plotArea.x,
                              agentGraph.plotArea.width,
                              xAxis.min,
                              xAxis.max
                          )
            onMaxChanged: chartSettings.setPlotMetrics(
                              agentGraph.x + agentGraph.plotArea.x,
                              agentGraph.plotArea.width,
                              xAxis.min,
                              xAxis.max
                          )
        }

        // ────────────────────────────────────────
        // BLUE current-time stripe
        // ────────────────────────────────────────
        Rectangle {
            id: currentTimeLine
            z: 2
            width: 1
            height: agentGraph.plotArea.height

            // plotArea.y is local to agentGraph → add agentGraph.y
            y: agentGraph.y + agentGraph.plotArea.y

            color: "blue"

            // playbarX comes from C++ in chartArea/global coords
            x: chartSettings.playbarX

            visible: true
        }
    }
}
