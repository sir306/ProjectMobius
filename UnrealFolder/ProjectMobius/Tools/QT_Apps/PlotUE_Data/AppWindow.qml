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
    // HEADER: only uses Column (no anchors inside)
    // ────────────────────────────────────────────
    Column {
        id: header
        anchors { top: parent.top; left: parent.left; right: parent.right }
        spacing: 10
        padding: 10

        // 1) Status
        Text {
            text: wsMgr.connectionStatus
            font.pixelSize: 16
            color: wsMgr.connectionStatus === "Connected" ? "lightgreen" : "tomato"
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // 2) Chart title
        Label {
            text: chartSettings.title
            font.pixelSize: 24
            color: "white"
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Text {
        //     text: chartSettings.statusDisplay
        //     font.pixelSize: 14
        //     color: "white"
        //     anchors.horizontalCenter: parent.horizontalCenter
        //     renderType: Text.NativeRendering
        //     layer.enabled: true;
        //     layer.smooth: true
        // }

        Row {
            spacing: 12
            anchors.horizontalCenter: parent.horizontalCenter

            // ── Static label, never changes ──
            Text {
                id: labelText
                font.pixelSize: 14
                color: wsMgr.connectionStatus === "Connected" ? "lightgreen" : "tomato"
                text: "Measuring:"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignRight
                renderType: Text.NativeRendering
                layer.enabled: true;
                layer.smooth: true
            }

            // ── Dynamic values, repaints when time or count change ──
            Text {
                id: valueText
                font.pixelSize: 14
                color: "white"
                text: chartSettings.statusDisplay
                wrapMode: Text.WordWrap
                renderType: Text.NativeRendering
                layer.enabled: true;
                layer.smooth: true
            }
        }



        // Visibility toggle for scatters if we want it back
        // // 3) Toggle button
        // Row {
        //     spacing: 10
        //     anchors.horizontalCenter: parent.horizontalCenter

        //     Button {
        //         text: chartSettings.showAllPoints
        //               ? "Show Major-Y-Only Scatter Points"
        //               : "Show All Scatter Points"
        //         onClicked: chartSettings.setShowAllPoints(!chartSettings.showAllPoints)
        //     }
        // }
    }

    // ────────────────────────────────────────────
    // CHART: anchored below header
    // ────────────────────────────────────────────
    Item {
        id: chartArea
        anchors {
            top:    header.bottom
            left:   parent.left
            right:  parent.right
            bottom: parent.bottom
        }

        // ────────────────────────────────────────────
        // 1) The actual GraphsView, now with only series in it
        // ────────────────────────────────────────────
        GraphsView {
            id: agentGraph
            anchors.fill: parent

            axisX: ValueAxis {
                id: xAxis
                min: axisSettings.xMin
                max: axisSettings.xChartMax

                //max: axisSettings.xMax
                titleText: axisSettings.xTitle
                gridVisible: axisSettings.xGridVisible
            }
            axisY: ValueAxis {
                id: yAxis
                min: axisSettings.yMin
                max: axisSettings.yChartMax


                titleText: axisSettings.yTitle
                gridVisible: axisSettings.yGridVisible
            }

            LineSeries    { id: lineSeries;    color: "red"   }
            ScatterSeries {
                id: scatterSeries
                pointDelegate: ScatterSeriesHoverItem {

                }
            }

            XYModelMapper { model: chartModel; series: lineSeries;    xSection:0; ySection:1 }
            XYModelMapper { model: chartModel; series: scatterSeries; xSection:0; ySection:1 }
        }
        Connections {
            target: agentGraph
            onPlotAreaChanged: chartSettings.setPlotMetrics(
                                   agentGraph.plotArea.x,
                                   agentGraph.plotArea.width,
                                   xAxis.min,
                                   xAxis.max
                                   )
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


        // ────────────────────────────────────────────
        // 2) The BLUE “currentTime” stripe, _outside_ the GraphsView
        // ────────────────────────────────────────────
        // Rectangle {
        //     id: currentTimeLine
        //     z:      1                  // on top of the GraphsView
        //     width:  2
        //     height: agentGraph.plotArea.height
        //     y:      agentGraph.plotArea.y
        //     color:  "blue"
        //     // property real xRange: xAxis.max - xAxis.min
        //     // property real plotX: agentGraph.plotArea.x
        //     // property real plotWidth: agentGraph.plotArea.width

        //     // x: plotX + ((chartSettings.currentTime - xAxis.min) / xRange) * plotWidth
        //     x: chartSettings.playbarX

        //     // x: agentGraph.plotArea.x
        //     //    + ((chartSettings.currentTime - xAxis.min)
        //     //       / (xAxis.max - xAxis.min))
        //     //    * agentGraph.plotArea.width

        // }

        // ShaderEffect {
        //     id: playbarEffect
        //     x: agentGraph.plotArea.x
        //     y: agentGraph.plotArea.y
        //     width: agentGraph.plotArea.width
        //     height: agentGraph.plotArea.height

        //     // Use the correct property names from AxisSettings
        //     property real inMinTime: axisSettings.xMin          // Correct property name
        //     property real inMaxTime: axisSettings.xChartMax     // Correct property name
        //     property real inPixelSize: 2.0 / agentGraph.plotArea.width

        //     // Since ChartSettings already calculates playbarX correctly,
        //     // convert it to normalized coordinates (0.0 to 1.0)
        //     property real inRelativeTime: agentGraph.plotArea.width > 0
        //         ? (chartSettings.playbarX - axisSettings.xMin) / (axisSettings.xChartMax - axisSettings.xMin)
        //         : 0.0

        //     // Pass time values to shader
        //     property vector4d inVars: Qt.vector4d(inRelativeTime, inMinTime, inMaxTime, inPixelSize)
        //     fragmentShader: "shaders/PlaybarShaderEffect.frag.qsb"

        // }
        Rectangle {
            id: currentTimeLine
            z: 2
            width: 1
            height: agentGraph.plotArea.height
            y: agentGraph.plotArea.y
            color: "blue"
            x: chartSettings.playbarX
            visible: true
        }


        // ShaderEffect {
        //     id: playbarEffect
        //     anchors.fill: chartArea

        //     // ───────────────────────────────────────────────────
        //     // 1) Place the effect at exactly (chart.x + plotArea.x, chart.y + plotArea.y)
        //     // ───────────────────────────────────────────────────
        //     x:      agentGraph.x + agentGraph.plotArea.x
        //     y:      agentGraph.y + agentGraph.plotArea.y
        //     width:  agentGraph.plotArea.width
        //     height: agentGraph.plotArea.height

        //     // ───────────────────────────────────────────────────
        //     // 2) Expose the five “property real” values into qt_ubo:
        //     //    - playbarXGlobal  → from chartSettings.playbarX
        //     //    - plotOriginX     → agentGraph.x + plotArea.x
        //     //    - plotY           → agentGraph.y + plotArea.y
        //     //    - plotH           → agentGraph.plotArea.height
        //     //    - canvasW         → agentGraph.plotArea.width
        //     //    - qt_Opacity      → 1.0
        //     //    - localPlayX      → (playbarXGlobal - plotOriginX)
        //     // ───────────────────────────────────────────────────
        //     property real playbarXGlobal: chartSettings.playbarX
        //     property real plotOriginX:    agentGraph.x + agentGraph.plotArea.x
        //     property real plotY:          agentGraph.y + agentGraph.plotArea.y
        //     property real plotH:          agentGraph.plotArea.height
        //     property real canvasW:        agentGraph.plotArea.width
        //     property real qt_Opacity:     1.0

        //     // localPlayX must exactly match the name in our GLSL’s uniform block:
        //     property real localPlayX: playbarXGlobal - plotOriginX

        //     // ───────────────────────────────────────────────────
        //     // 3) Point at the compiled SPIR-V blob.  Because CMake embedded it
        //     //    under “:/PlotUE_Data/shaders/PlaybarShaderEffect.frag.qsb”, and
        //     //    your QML import path already knows to look in that module,
        //     //    you can just write exactly “shaders/PlaybarShaderEffect.frag.qsb”.
        //     // ───────────────────────────────────────────────────
        //     fragmentShader: "shaders/PlaybarShaderEffect.frag.qsb"

        //     Component.onCompleted: {
        //         console.log(
        //             "▶  playbarXGlobal =", playbarEffect.playbarXGlobal,
        //             " plotOriginX =", playbarEffect.plotOriginX,
        //             " localPlayX =", playbarEffect.localPlayX,
        //             " canvasW =", playbarEffect.canvasW,
        //             " plotY =", playbarEffect.plotY,
        //             " plotH =", playbarEffect.plotH,
        //             " effect.x/y/w/h =", playbarEffect.x, playbarEffect.y,
        //                                  playbarEffect.width, playbarEffect.height
        //         );
        //     }
        // }



        // // BLUE playbar stripe
        // Rectangle {
        //     id: currentTimeLine
        //     z:      1
        //     width:  2
        //     height: agentGraph.plotArea.height
        //     y:      agentGraph.plotArea.y
        //     color:  "blue"
        //     x:      chartSettings.playbarX
        // }


    }
}
