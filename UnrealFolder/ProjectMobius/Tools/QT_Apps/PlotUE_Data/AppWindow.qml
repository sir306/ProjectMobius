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

        // ── Chart title only ──
        Label {
            text: chartSettings.title
            font.pixelSize: 24
            color: "white"
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
        }
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

            // 1 device pixel helper
                // (Qt rounds to nearest device pixel and avoids blurry/“fat” lines)
                property real px: 1.0 / window.screen.devicePixelRatio

                // Disable AA for crisp hairlines
                antialiasing: false

                theme: GraphsTheme {
                    theme: GraphsTheme.Theme.UserDefined

                    // backgrounds
                    backgroundVisible: true
                    backgroundColor: "white"
                    plotAreaBackgroundVisible: true
                    plotAreaBackgroundColor: "white"

                    // axis styling
                    axisX.labelTextColor: "black"
                    axisY.labelTextColor: "black"
                    axisX.mainColor: "black"     // axis baseline
                    axisY.mainColor: "black"
                    axisX.subColor:  "black"     // tick marks
                    axisY.subColor:  "black"

                    // ensure axis and tick widths are exactly 1 device pixel
                    axisX.mainWidth: agentGraph.px
                    axisY.mainWidth: agentGraph.px
                    axisX.subWidth:  agentGraph.px
                    axisY.subWidth:  agentGraph.px

                    // grid styling
                    gridVisible: true
                    grid.mainColor: "black"
                    grid.subColor:  "black"

                    // Make lines 1 device pixel;
                    grid.mainWidth: agentGraph.px
                    grid.subWidth:  0            // <- disables visible minor grid thickness
                }


                axisX: ValueAxis {
                    id: xAxis
                    min: axisSettings.xMin
                    max: axisSettings.xChartMax
                    titleText: axisSettings.xTitle
                    gridVisible: axisSettings.xGridVisible
                    titleColor: "black"
                    subTickCount: 0
                }

                axisY: ValueAxis {
                    id: yAxis
                    min: axisSettings.yMin
                    max: axisSettings.yChartMax
                    titleText: axisSettings.yTitle
                    gridVisible: axisSettings.yGridVisible
                    titleColor: "black"
                    subTickCount: 0
                }

            LineSeries    { id: lineSeries;    color: "dark green"   }
            ScatterSeries {
                id: scatterSeries
                pointDelegate: GraphHoverItem {

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
