#version 330 core
// uMode 0: show uTex0.   uMode 1: mix(uTex0, uTex1, uBlend) (reference/inspected blend).
// Grey textures are expanded to RGB by the texture swizzle (R,R,R,1), BGR data is
// reordered by the upload format, so the shader always sees plain RGB.
in vec2 vUv;

uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform int uMode;
uniform float uBlend;

out vec4 fragColor;

void main() {
    vec3 a = texture(uTex0, vUv).rgb;
    if (uMode == 1) {
        vec3 b = texture(uTex1, vUv).rgb;
        a = mix(a, b, uBlend);
    }
    fragColor = vec4(a, 1.0);
}
