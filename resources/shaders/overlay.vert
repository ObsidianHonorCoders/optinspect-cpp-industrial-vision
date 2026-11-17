#version 330 core
// Overlay primitives (boxes, selection, ROI) as coloured triangles.
// aPos is in viewport-local pixels (origin top-left, y down); uViewportSize is
// the viewport size in the same unit, so the overlay shares the exact
// image-to-screen mapping with the image quad.
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;

uniform vec2 uViewportSize;

out vec4 vColor;

void main() {
    vColor = aColor;
    vec2 ndc = vec2(aPos.x / uViewportSize.x * 2.0 - 1.0, 1.0 - aPos.y / uViewportSize.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
