precision highp float;

uniform float time;
uniform float windAngle;
uniform float windSpeed;
uniform float windAlpha;
uniform vec2 windOffset;

attribute vec3 position;
attribute vec3 normal;
attribute vec2 uv;

uniform mat4 worldViewProjection;
uniform mat4 world;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 cameraPosition;
uniform vec2 screensize;

varying vec4 vPosition;
varying vec4 rippleUV;
varying vec4 waveUV;
varying vec4 windArrowsUV;
varying vec4 floorUV;
varying vec3 vCameraPosition;
varying vec3 vViewDir;
varying vec2 vCoastUV;

vec4 rippleScale = vec4(1.5, 0.9, 0.7, 1.2);
vec4 rippleSpeed = vec4(-0.1, -0.3, 0.2, 0.3);
vec4 waveScale = vec4(0.10, 0.06, 0.1, 0.1);
vec4 waveSpeed = vec4(-0.03, -0.04, 0.06, 0.06);
vec4 floorPos = vec4(0.005, 0.005, 0.0, 0.0);

mat3 GLSLtranspose(mat3 m) {
    return mat3(
        m[0][0], m[1][0], m[2][0],
        m[0][1], m[1][1], m[2][1],
        m[0][2], m[1][2], m[2][2]
    );
}

void main(void) {
    gl_Position = worldViewProjection * vec4(position, 1.0);

    vec3 vertPosition = (world * vec4(position, 1.0)).xyz;
    vCoastUV = uv;

    float arrowScale = 0.002;
    float cosW = cos(windAngle);
    float sinW = sin(windAngle);
    windArrowsUV.x = (vertPosition.x * cosW - vertPosition.z * sinW) * arrowScale + windOffset.x;
    windArrowsUV.y = (vertPosition.x * sinW + vertPosition.z * cosW) * arrowScale + windOffset.y;
    windArrowsUV.z = windSpeed;
    windArrowsUV.w = windAlpha;

    float dist = distance(cameraPosition, vertPosition) / 2000.0;
    dist = clamp(dist, 0.0, 1.0);
    vPosition = vec4(vertPosition.xyz, dist);

    vec2 vUV = vertPosition.xz * 0.02;
    rippleUV = vUV.xyxy * rippleScale + rippleSpeed * time;
    waveUV = vUV.xyxy * waveScale + waveSpeed * time;
    floorUV = vUV.xyxy * floorPos.xyxy + floorPos.zwzw;

    vCameraPosition = cameraPosition;

    mat3 TBN = GLSLtranspose(mat3(
        vec3(1.0, 0.0, 0.0),
        vec3(0.0, 0.0, 1.0),
        vec3(0.0, 1.0, 0.0)
    ));
    vec3 tangentViewPos = TBN * cameraPosition;
    vec3 tangentFragPos = TBN * vertPosition;
    vViewDir = normalize(tangentViewPos - tangentFragPos);
}
