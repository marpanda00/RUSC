precision highp float;

// Varyings (max 8 for iOS)
varying vec4 vPosition;
varying vec4 rippleUV;
varying vec4 waveUV;
varying vec4 windArrowsUV;
varying vec4 floorUV;
varying vec3 vCameraPosition;
varying vec3 vViewDir;
varying vec3 vScreenViewDir;

vec4 vRippleScale = vec4(1.5,  0.9,  0.7,  1.2);
vec4 vWaveScale   = vec4(0.10, 0.06, 0.1,  0.1);
vec4 vFoamScale   = vec4(1.0,  1.0,  1.1,  0.8);

// Uniforms
uniform mat4 world;
uniform mat4 view;
uniform mat4 projection;
uniform vec2 screensize;
uniform float time;

// Textures  (WebGL spec guarantees 8 texture units)
uniform sampler2D bumpMap;          // primary wave normal map
uniform sampler2D rippleBump;       // detail ripple normal map
uniform sampler2D heightMap;        // parallax height map
uniform sampler2D waterFoam;        // foam / whitecap texture
uniform samplerCube cubeMap;        // skybox for reflections
uniform sampler2D arrowTexture;     // wind direction arrow sprite
uniform sampler2D waterFresnel;     // 1D fresnel LUT
uniform sampler2D underwaterTexture;// seafloor / underwater base

// Tweakable constants
float floorDistortion = 0.2;
float floorDepth      = 0.01;
vec4  floorPos        = vec4(0.005, 0.005, 0.0, 0.0);

float baseWaveDistortion = 0.018;
float ambientPickup      = 0.2;

vec4 baseColor      = vec4(0.2,  0.36, 0.36, 0.25);
vec4 reflectionTint = vec4(0.95, 1.0,  1.0,  0.3);
vec4 SSSColor       = vec4(0.44, 0.84, 1.0,  0.75);

float _Scale = 0.9;
float _Power = 6.0;
float _Base  = 0.0;

float height_scale      = 0.03;
const float numParallaxLayers = 3.0;

// ─── Utility functions ───────────────────────────────────────────────────────

vec4 getBlendedTex(sampler2D tex, vec4 uv) {
    return (texture2D(tex, uv.xy).rgba + texture2D(tex, uv.zw).rgba) * 0.6;
}

vec4 getMultipliedTex(sampler2D tex, vec4 uv) {
    return texture2D(tex, uv.xy).rgba * texture2D(tex, uv.zw).rgba;
}

vec3 UnpackBumpMap(sampler2D tex, vec2 uv) {
    return texture2D(tex, uv).rgb * 2.0 - 1.0;
}

vec3 UnpackGetBump(sampler2D tex, vec4 uv) {
    vec3 bump = texture2D(tex, uv.xy).rgb + texture2D(tex, uv.zw).rgb;
    return (bump * 2.0 - 2.0) * 0.5;
}

vec3 UnpackGetBumpRipped(sampler2D tex, vec4 uv) {
    vec3 bump = texture2D(tex, uv.xy).rgb;
    bump += texture2D(tex, uv.zw - bump.xz * 0.03).rgb;
    return (bump * 2.0 - 2.0) * 0.5;
}

// Blend two normal maps correctly (vs naive averaging)
vec3 combineNormalMaps(vec3 base, vec3 detail) {
    base   += vec3(0.0, 0.0, 1.0);
    detail *= vec3(-1.0, -1.0, 1.0);
    return base * dot(base, detail) / base.z - detail;
}

float GetWaveHeightValue(sampler2D heightTex, vec4 uv) {
    vec3 wave = texture2D(heightTex, uv.xy).rgb + texture2D(heightTex, uv.zw).rgb;
    return (wave * 0.5).r;
}

vec3 lerp(vec3 a, vec3 b, float f) {
    return a * (1.0 - f) + b * f;
}

// Steep parallax offset (adapted from learnopengl.com)
vec2 SteepParallaxOffset(vec2 texCoords, vec3 viewDir) {
    float layerDepth      = 1.0 / numParallaxLayers;
    float currentDepth    = 0.0;
    vec2  P               = viewDir.xy * height_scale;
    vec2  deltaUV         = P / numParallaxLayers;

    vec2  curUV           = texCoords;
    float curDepthVal     = 1.0 - texture2D(heightMap, curUV).r;

    for (int i = 0; i < int(numParallaxLayers); i++) {
        if (currentDepth < curDepthVal) {
            curUV      -= deltaUV;
            curDepthVal = 1.0 - texture2D(heightMap, curUV).r;
            currentDepth += layerDepth;
        }
    }

    vec2  prevUV       = curUV + deltaUV;
    float afterDepth   = curDepthVal - currentDepth;
    float beforeDepth  = 1.0 - texture2D(heightMap, prevUV).r - currentDepth + layerDepth;
    float weight       = afterDepth / (afterDepth - beforeDepth);
    vec2  finalUV      = prevUV * weight + curUV * (1.0 - weight);

    return texCoords - finalUV;
}

vec2 SimpleParallaxMapping(vec2 texCoords, vec3 viewDir, float height, float power) {
    return ((viewDir.xy) / viewDir.z) * (height * height_scale * power);
}

vec4 CrossConvertOffset(vec4 baseOffset, vec4 fromCoords, vec4 toCoords) {
    return (baseOffset / fromCoords) * toCoords;
}

// ─── Main ────────────────────────────────────────────────────────────────────

void main(void) {
    vec3 lightVectorW    = normalize(vec3(0.4, 0.4, 0.4));
    vec3 vPositionW      = vec3(world * vec4(vPosition.xyz, 1.0));
    vec3 viewDirectionW  = normalize(vCameraPosition - vPositionW);

    // Parallax offsets for primary + secondary wave layers
    vec2 baseMapCoords      = SteepParallaxOffset(waveUV.xy, vViewDir);
    vec2 secondaryMapCoords = SteepParallaxOffset(waveUV.zw, vViewDir);
    vec4 parallaxOffset     = vec4(baseMapCoords, secondaryMapCoords);

    float waveHeight   = GetWaveHeightValue(heightMap, waveUV - parallaxOffset);
    vec3  baseWaveBump = UnpackGetBump(bumpMap, waveUV - parallaxOffset);

    vec2 combinedParallax  = (parallaxOffset.xy + parallaxOffset.zw) * 0.5;
    vec4 offsetParallax    = CrossConvertOffset(combinedParallax.xyxy, vWaveScale, vRippleScale);

    vec3 bumpTex = UnpackGetBump(rippleBump, rippleUV - offsetParallax.zwzw - baseWaveBump.xyyz * 0.05);
    bumpTex = normalize(bumpTex);
    bumpTex = combineNormalMaps(baseWaveBump, bumpTex);
    bumpTex = lerp(baseWaveBump, bumpTex, 0.25 + waveHeight * 1.0);

    // Foam
    offsetParallax = CrossConvertOffset(combinedParallax.xyxy, vWaveScale, vFoamScale);
    vec4 foamTex   = getBlendedTex(waterFoam, rippleUV.zwxy - offsetParallax.zwzw).rgba;

    // Normal in world space
    vec3 vNormalW = normalize(combineNormalMaps(vec3(0.0, 1.0, 0.0), bumpTex.rgb));

    // Diffuse
    float ndl = max(0.0, dot(vNormalW, lightVectorW)) + ambientPickup;

    // Specular
    vec3  angleW   = normalize(viewDirectionW + lightVectorW);
    float specComp = max(0.0, pow(dot(vNormalW, angleW), 16.0));

    // Flatten normals at distance to avoid shimmering
    vNormalW = lerp(vNormalW, vec3(0.0, 1.0, 0.0), vPosition.w);

    // Cubemap reflection
    vec3 reflectedDirection = normalize(reflect(viewDirectionW, normalize(vNormalW)));
    vec4 cubeColor          = textureCube(cubeMap, -reflectedDirection);
    vec3 screenReflection   = cubeColor.rgb;

    // Fresnel (LUT lookup)
    float lightDot  = clamp(dot(viewDirectionW, normalize(vNormalW)), 0.0, 1.0);
    vec4  fresnelTex = texture2D(waterFresnel, vec2(lightDot, 0.5));
    float fresnelTerm = fresnelTex.r;

    // Subsurface scattering approximation
    float SSSspec = pow(specComp, 0.5) * waveHeight * waveHeight;

    specComp = pow(specComp, max(1.0, 64.0));

    float waveHeightSharpened = clamp(10.0 * (waveHeight - 0.8), 0.0, 1.0);

    // Wind arrows (animated with sine wave distortion)
    vec4 windArrowDistortUV = vec4(
        windArrowsUV.x + sin((windArrowsUV.y + time) * 6.0) * 0.02,
        windArrowsUV.y,
        windArrowsUV.z,
        windArrowsUV.w
    );
    vec4 windArrows = texture2D(arrowTexture, windArrowDistortUV.xy).rgba;

    // Underwater / seafloor
    vec4 underwaterTex = texture2D(
        underwaterTexture,
        floorUV.xy * floorPos.xy * 2.0 + floorPos.zw
        - viewDirectionW.xz / viewDirectionW.y * floorDepth
        - vNormalW.xy * floorDistortion
    );

    // Composite
    vec4 finalColor = vec4(
        lerp(baseColor.rgb, underwaterTex.rgb, baseColor.a)
        + SSSColor.rgb * 1.2 * SSSspec * (1.0 - fresnelTerm),
        1.0
    );

    finalColor = vec4(
        lerp(
            finalColor.rgb * ndl
            + screenReflection.rgb * reflectionTint.rgb * reflectionTint.a * fresnelTerm * ndl * 1.5
            + vec3(clamp(specComp * 1.2, 0.0, 1.0)),
            foamTex.rgb * ndl,
            waveHeightSharpened * foamTex.a
        ),
        1.0
    );

    // Wind arrow overlay
    float arrowAlpha = windArrows.a;
    finalColor = vec4(lerp(finalColor.rgb, vec3(1.0, 1.0, 1.0), arrowAlpha * windArrowsUV.w), 1.0);

    gl_FragColor = finalColor;
}
