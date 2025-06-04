// Shaders/MyPlaybar.frag
#version 450

// ────────────────────────────────────────────────
// Qt6’s SPIR-V compiler expects:
//   layout(std140, binding = 0) uniform qt_ubo { … };
// and no stray `uniform float x;` outside of a block.
// ────────────────────────────────────────────────
layout(std140, binding = 0) uniform qt_ubo {
    float localPlayX;   // local: “playbarXGlobal - plotX”
    float plotY;
    float plotH;
    float canvasW;
    float qt_Opacity;
};

// In Qt 6 SPIR-V, use “in” for per-fragment UVs and an explicit out var.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

void main() {
    // 1) localX runs from 0 → canvasW (e.g. 0→540) as qt_TexCoord0.x goes 0→1
    float localX = qt_TexCoord0.x * canvasW;

    // 2) localY runs from 0 → plotH; then + plotY yields global Y (range 20→350)
    float localY = qt_TexCoord0.y * plotH + plotY;

    // 3) Compare “horizontal distance” in LOCAL space:
    //    abs(localX – localPlayX) < 1.0 draws a 1px-wide stripe *inside* the effect.
    if (abs(localX - localPlayX) < 1.0
        && localY >= plotY
        && localY <= (plotY + plotH)) {
        fragColor = vec4(0.2, 0.6, 1.0, qt_Opacity);
    } else {
        discard;
    }
}
