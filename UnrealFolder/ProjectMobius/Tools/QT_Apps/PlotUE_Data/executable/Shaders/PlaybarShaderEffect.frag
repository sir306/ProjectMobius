#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

// ✅ Add instance name `buf` after the closing brace
layout(std140, binding = 0) uniform bufType {
    float playbarX;
    float plotY;
    float plotH;
    float canvasW;
    float qt_Opacity;
} buf;  // ← this is what was missing

void main() {
    float px = qt_TexCoord0.x * buf.canvasW;
    float py = qt_TexCoord0.y * buf.canvasW;

    if (abs(px - buf.playbarX) < 1.0 && py >= buf.plotY && py <= (buf.plotY + buf.plotH)) {
        fragColor = vec4(0.2, 0.6, 1.0, buf.qt_Opacity);
    } else {
        //discard;
    }
}
