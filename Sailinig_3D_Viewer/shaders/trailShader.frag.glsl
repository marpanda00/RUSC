#ifdef GL_ES
    precision highp float;
#endif

varying vec2 vUV;
varying vec4 vColor;

uniform sampler2D mainTex;

void main(void) {
    vec4 lineTex = texture2D(mainTex, vUV).rgba;
    gl_FragColor = lineTex.rgba * vColor.rgba;
}
