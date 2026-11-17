#version 330 core
// Textured image quad. aPos is the unit quad: (0,0) = image top-left,
// (1,1) = image bottom-right. uRect places that quad in the normalized device
// coordinates of the current glViewport: (left, top, width, height) with a
// NEGATIVE height because NDC y points up while image y points down.
// The texture coordinate equals aPos: OpenCV row 0 is uploaded first, so
// t = 0 is the image top and no vertical flip is required.
layout(location = 0) in vec2 aPos;

uniform vec4 uRect;

out vec2 vUv;

void main() {
    vUv = aPos;
    gl_Position = vec4(uRect.xy + aPos * uRect.zw, 0.0, 1.0);
}
