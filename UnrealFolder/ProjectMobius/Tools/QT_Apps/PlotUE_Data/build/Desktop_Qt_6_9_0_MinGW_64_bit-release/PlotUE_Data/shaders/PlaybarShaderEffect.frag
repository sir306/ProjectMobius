#version 450
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 inVars;
} ubuf;

void main() {
    float timePos = ubuf.inVars.x;       // Current time position (e.g., 0.15)
    float minTime = ubuf.inVars.y;       // Min time (0.0)
    float maxTime = ubuf.inVars.z;       // Max time (1.0)
    float pixelSize = ubuf.inVars.w;     // Line thickness

    // Convert time to normalized position [0,1] within the chart
    float timeRange = max(maxTime - minTime, 0.001); // Prevent division by zero
    float timeNormalized = (timePos - minTime) / timeRange;

    // Clamp to valid range
    timeNormalized = clamp(timeNormalized, 0.0, 1.0);

    // Current fragment X position in normalized coordinates [0,1]
    float currentX = qt_TexCoord0.x;

    // Draw vertical line across entire height (no Y restriction)
    // Check if we're within the line thickness of the playbar position
    if (abs(currentX - timeNormalized) <= (pixelSize * 0.5)) {
        fragColor = vec4(0.2, 0.6, 1.0, 0.8); // Blue line with slight transparency
    } else {
        fragColor = vec4(0.0, 0.0, 0.0, 0.0); // Transparent
    }
}
// layout(std140, binding = 1) uniform buf {
//     float inLocalPlayX;   // from QML: property real localPlayX
//     float inPlotY;        // from QML: property real plotY
//     float inPlotH;        // from QML: property real plotH
//     float inCanvasW;
// };
// void main() {
//     // Map playbar position to 0-1 range based on your coordinate system
//         // float playbarNormalized = inLocalPlayX / inCanvasW;
//         // float stripeWidth = 10.0 / inCanvasW;

//         // // Use Y coordinate but compare against normalized position
//         // if (abs(qt_TexCoord0.y - playbarNormalized) < stripeWidth * 0.5) {
//         //     fragColor = vec4(0.2, 0.6, 1.0, 1.0);
//         // } else {
//         //     fragColor = vec4(1,1,0,1);
//         //     //discard;
//         // }

//         fragColor = vec4(1,1,0,1);
// }
