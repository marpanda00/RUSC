#ifdef GL_ES
    precision highp float;
#endif

varying vec3 vPosition;
varying vec3 vNormal;
varying vec2 vUV;
varying vec4 vColor;
varying vec4 waveUV;
varying vec3 vViewDir;
varying vec3 vCameraPosition;

uniform sampler2D wakeTexture;
uniform sampler2D waveBump;
uniform sampler2D heightMap;
uniform mat4 world;

void main(void) {
    vec4 wakeTex = texture2D(wakeTexture, vUV).rgba;
    wakeTex.a *= vColor.a;
    gl_FragColor = wakeTex.rgba * vColor.rgba;
}
