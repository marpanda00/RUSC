#ifdef GL_ES
    precision highp float;
#endif

// Attributes
uniform float time;
attribute vec3 position;
attribute vec2 uv;
attribute vec4 color;

// Uniforms
uniform mat4 worldViewProjection;
uniform mat4 world;
uniform sampler2D wakeTexture;
uniform vec3 cameraPosition;

// Varyings
varying vec2 vUV;
varying vec2 vWorldUV;
varying vec4 vColor;
varying vec4 waveUV;
varying vec3 vViewDir;
varying vec3 vPosition;
varying vec3 vNormal;
varying vec3 vCameraPosition;

vec4 waveScale = vec4(0.06, 0.03, 0.1, 0.1);
vec4 waveSpeed = vec4(-0.03,-0.04, 0.06, 0.06);

mat3 GLSLtranspose(mat3 m) {
  return mat3(m[0][0], m[1][0], m[2][0],
              m[0][1], m[1][1], m[2][1],
              m[0][2], m[1][2], m[2][2]);
}

void main(void) {
    gl_Position = worldViewProjection * vec4(position, 1.0);

    vPosition   = (world * vec4(position, 1.0)).xyz;
    vWorldUV    = vPosition.xz * 0.02;
    waveUV      = vWorldUV.xyxy * waveScale.xyzw + waveSpeed.xyzw * time;
    vUV         = uv;
    vColor      = color;
    vCameraPosition = cameraPosition;
}
