precision highp float;

uniform float time;
uniform vec3 vCornerObjects[4];
uniform vec3 screenDir;
uniform float windAngle;
uniform float windSpeed, windAlpha;
uniform vec2 windOffset;

// Attributes
attribute vec3 position;
attribute vec3 normal;
attribute vec2 uv;
attribute vec4 tangent;

// Uniforms
uniform mat4 worldViewProjection, world, view, projection;
uniform vec3 cameraPosition;
uniform vec2 screensize;

// Varyings  (capped at 8 for iOS)
varying vec4 vPosition;
varying vec4 rippleUV;
varying vec4 waveUV;
varying vec4 windArrowsUV;
varying vec4 floorUV;
varying vec3 vCameraPosition;
varying vec3 vViewDir;
varying vec3 vScreenViewDir;

// UV animation constants
vec4 rippleScale = vec4(1.5,  0.9,  0.7,  1.2);
vec4 rippleSpeed = vec4(-0.1,-0.3,  0.2,  0.3);

vec4 waveScale   = vec4(0.10, 0.06, 0.1,  0.1);
vec4 waveSpeed   = vec4(-0.03,-0.04,0.06, 0.06);

vec4 foamScale   = vec4(1.0,  1.0,  1.1,  0.8);
vec4 foamSpeed   = vec4(0.6,  0.4,  0.4, -0.4);

vec4 floorPos    = vec4(0.005,0.005, 0.0,  0.0);

mat3 GLSLtranspose(mat3 m) {
    return mat3(m[0][0], m[1][0], m[2][0],
                m[0][1], m[1][1], m[2][1],
                m[0][2], m[1][2], m[2][2]);
}

void main(void) {
    gl_Position = worldViewProjection * vec4(position, 1.0);

    vec3 vertPosition = (world * vec4(position, 1.0)).xyz;

    // Wind arrow UVs — packed as vec4: .xy=arrowUV, .z=speed, .w=alpha
    float arrowScale = 0.002;
    float cosW = cos(windAngle);
    float sinW = sin(windAngle);
    windArrowsUV.x = (vertPosition.x * cosW - vertPosition.z * sinW) * arrowScale + windOffset.x;
    windArrowsUV.y = (vertPosition.x * sinW + vertPosition.z * cosW) * arrowScale + windOffset.y;
    windArrowsUV.z = windSpeed;
    windArrowsUV.w = windAlpha;

    // Flat normal assumption (water is flat)
    vec3 nrml = vec3(0.0, 1.0, 0.0);
    float dist = distance(cameraPosition, vertPosition) / 2000.0;
    dist = clamp(dist, 0.0, 1.0);
    vPosition = vec4(vertPosition.xyz, dist);

    // World-position UVs
    vec2 vUV = vertPosition.xz * 0.02;

    rippleUV      = vUV.xyxy * rippleScale.xyzw + rippleSpeed.xyzw * time;
    waveUV        = vUV.xyxy * waveScale.xyzw   + waveSpeed.xyzw   * time;
    floorUV       = vUV.xyxy * floorPos.xyxy    + floorPos.zwzw;

    vCameraPosition = cameraPosition;

    // TBN for parallax (flat-water assumption)
    vec3 T   = vec3(1.0, 0.0, 0.0);
    vec3 B   = vec3(0.0, 0.0, 1.0);
    vec3 N   = vec3(0.0, 1.0, 0.0);
    mat3 TBN = GLSLtranspose(mat3(T, B, N));

    vec3 TangentViewPos  = TBN * cameraPosition;
    vec3 TangentFragPos  = TBN * vertPosition.xyz;
    vViewDir = normalize(TangentViewPos - TangentFragPos);
}
