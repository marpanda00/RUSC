#ifdef GL_ES
    precision highp float;
#endif

uniform float uvOffset;
uniform float fade;
attribute vec3 position;
attribute vec2 uv;
attribute vec4 color;

uniform mat4 worldViewProjection;
uniform sampler2D wakeTexture;

varying vec4 vColor;
varying vec2 vUV;

void main(void) {
    gl_Position = worldViewProjection * vec4(position, 1.0);
    vUV   = vec2(uv.x, uv.y);
    vColor = vec4(color.r, color.g, color.b, color.a * fade);
}
