precision highp float;

varying vec4 vPosition;
varying vec4 rippleUV;
varying vec4 waveUV;
varying vec4 windArrowsUV;
varying vec4 floorUV;
varying vec3 vCameraPosition;
varying vec3 vViewDir;
varying vec2 vCoastUV;

uniform mat4 world;
uniform mat4 view;
uniform mat4 projection;
uniform vec2 screensize;
uniform float time;
uniform float useCoastMask;
uniform float waterOpacity;
// Coast mask (computed from world Z/X). dir: 0=north 1=south 2=west 3=east.
// "land is north" means land occupies the −Z side of the shoreline.
uniform float coastDir;
uniform float shorelineM;
uniform float fadeM;

uniform sampler2D bumpMap;
uniform sampler2D rippleBump;
uniform sampler2D heightMap;
uniform sampler2D waterFoam;
uniform samplerCube cubeMap;
uniform sampler2D arrowTexture;
uniform sampler2D waterFresnel;
uniform sampler2D underwaterTexture;

vec4 vRippleScale = vec4(1.5, 0.9, 0.7, 1.2);
vec4 vWaveScale = vec4(0.10, 0.06, 0.1, 0.1);
vec4 vFoamScale = vec4(1.0, 1.0, 1.1, 0.8);

float floorDistortion = 0.2;
float floorDepth = 0.01;
vec4 floorPos = vec4(0.005, 0.005, 0.0, 0.0);
float ambientPickup = 0.2;
vec4 baseColor = vec4(0.2, 0.36, 0.36, 0.25);
vec4 reflectionTint = vec4(0.95, 1.0, 1.0, 0.3);
vec4 SSSColor = vec4(0.44, 0.84, 1.0, 0.75);
float height_scale = 0.03;
const float numParallaxLayers = 3.0;

vec4 getBlendedTex(sampler2D tex, vec4 uv) {
    return (texture2D(tex, uv.xy) + texture2D(tex, uv.zw)) * 0.6;
}

vec3 UnpackGetBump(sampler2D tex, vec4 uv) {
    vec3 bump = texture2D(tex, uv.xy).rgb + texture2D(tex, uv.zw).rgb;
    return (bump * 2.0 - 2.0) * 0.5;
}

vec3 combineNormalMaps(vec3 base, vec3 detail) {
    base += vec3(0.0, 0.0, 1.0);
    detail *= vec3(-1.0, -1.0, 1.0);
    return base * dot(base, detail) / base.z - detail;
}

float GetWaveHeightValue(sampler2D heightTex, vec4 uv) {
    vec3 wave = texture2D(heightTex, uv.xy).rgb + texture2D(heightTex, uv.zw).rgb;
    return (wave * 0.5).r;
}

vec3 lerp3(vec3 a, vec3 b, float f) {
    return a * (1.0 - f) + b * f;
}

vec2 SteepParallaxOffset(vec2 texCoords, vec3 viewDir) {
    float layerDepth = 1.0 / numParallaxLayers;
    float currentDepth = 0.0;
    vec2 P = viewDir.xy * height_scale;
    vec2 deltaUV = P / numParallaxLayers;
    vec2 curUV = texCoords;
    float curDepthVal = 1.0 - texture2D(heightMap, curUV).r;

    for (int i = 0; i < int(numParallaxLayers); i++) {
        if (currentDepth < curDepthVal) {
            curUV -= deltaUV;
            curDepthVal = 1.0 - texture2D(heightMap, curUV).r;
            currentDepth += layerDepth;
        }
    }

    vec2 prevUV = curUV + deltaUV;
    float afterDepth = curDepthVal - currentDepth;
    float beforeDepth = 1.0 - texture2D(heightMap, prevUV).r - currentDepth + layerDepth;
    float weight = afterDepth / (afterDepth - beforeDepth);
    vec2 finalUV = prevUV * weight + curUV * (1.0 - weight);
    return texCoords - finalUV;
}

vec4 CrossConvertOffset(vec4 baseOffset, vec4 fromCoords, vec4 toCoords) {
    return (baseOffset / fromCoords) * toCoords;
}

// 0..1 sea visibility at world X/Z. +Z = south, −Z = north.
float seaMask(float x, float z) {
    float fade = max(20.0, fadeM);
    if (coastDir < 0.5) {
        // land north (−Z): water on +Z side of shoreline
        return smoothstep(shorelineM, shorelineM + fade, z);
    } else if (coastDir < 1.5) {
        // land south (+Z): water on −Z side
        return 1.0 - smoothstep(shorelineM - fade, shorelineM, z);
    } else if (coastDir < 2.5) {
        // land west (−X)
        return smoothstep(shorelineM, shorelineM + fade, x);
    }
    // land east (+X)
    return 1.0 - smoothstep(shorelineM - fade, shorelineM, x);
}

void main(void) {
    float coastA = 1.0;
    if (useCoastMask > 0.5) {
        coastA = seaMask(vPosition.x, vPosition.z);
        if (coastA < 0.02) discard;
    }

    vec3 lightVectorW = normalize(vec3(0.4, 0.4, 0.4));
    vec3 vPositionW = vec3(world * vec4(vPosition.xyz, 1.0));
    vec3 viewDirectionW = normalize(vCameraPosition - vPositionW);

    vec2 baseMapCoords = SteepParallaxOffset(waveUV.xy, vViewDir);
    vec2 secondaryMapCoords = SteepParallaxOffset(waveUV.zw, vViewDir);
    vec4 parallaxOffset = vec4(baseMapCoords, secondaryMapCoords);

    float waveHeight = GetWaveHeightValue(heightMap, waveUV - parallaxOffset);
    vec3 baseWaveBump = UnpackGetBump(bumpMap, waveUV - parallaxOffset);

    vec2 combinedParallax = (parallaxOffset.xy + parallaxOffset.zw) * 0.5;
    vec4 offsetParallax = CrossConvertOffset(combinedParallax.xyxy, vWaveScale, vRippleScale);

    vec3 bumpTex = UnpackGetBump(rippleBump, rippleUV - offsetParallax.zwzw - baseWaveBump.xyyz * 0.05);
    bumpTex = normalize(bumpTex);
    bumpTex = combineNormalMaps(baseWaveBump, bumpTex);
    bumpTex = lerp3(baseWaveBump, bumpTex, 0.25 + waveHeight);

    offsetParallax = CrossConvertOffset(combinedParallax.xyxy, vWaveScale, vFoamScale);
    vec4 foamTex = getBlendedTex(waterFoam, rippleUV.zwxy - offsetParallax.zwzw).rgba;

    vec3 vNormalW = normalize(combineNormalMaps(vec3(0.0, 1.0, 0.0), bumpTex));
    float ndl = max(0.0, dot(vNormalW, lightVectorW)) + ambientPickup;

    vec3 angleW = normalize(viewDirectionW + lightVectorW);
    float specComp = max(0.0, pow(dot(vNormalW, angleW), 16.0));

    vNormalW = lerp3(vNormalW, vec3(0.0, 1.0, 0.0), vPosition.w);

    vec3 reflectedDirection = normalize(reflect(viewDirectionW, normalize(vNormalW)));
    vec3 screenReflection = textureCube(cubeMap, -reflectedDirection).rgb;

    float lightDot = clamp(dot(viewDirectionW, normalize(vNormalW)), 0.0, 1.0);
    float fresnelTerm = texture2D(waterFresnel, vec2(lightDot, 0.5)).r;

    float SSSspec = pow(specComp, 0.5) * waveHeight * waveHeight;
    specComp = pow(specComp, 64.0);
    float waveHeightSharpened = clamp(10.0 * (waveHeight - 0.8), 0.0, 1.0);

    vec4 underwaterTex = texture2D(
        underwaterTexture,
        floorUV.xy * floorPos.xy * 2.0 + floorPos.zw
        - viewDirectionW.xz / viewDirectionW.y * floorDepth
        - vNormalW.xy * floorDistortion
    );

    vec4 finalColor = vec4(
        lerp3(baseColor.rgb, underwaterTex.rgb, baseColor.a)
        + SSSColor.rgb * 1.2 * SSSspec * (1.0 - fresnelTerm),
        1.0
    );

    finalColor = vec4(
        lerp3(
            finalColor.rgb * ndl
            + screenReflection * reflectionTint.rgb * reflectionTint.a * fresnelTerm * ndl * 1.5
            + vec3(clamp(specComp * 1.2, 0.0, 1.0)),
            foamTex.rgb * ndl,
            waveHeightSharpened * foamTex.a
        ),
        1.0
    );

    float alpha = waterOpacity * coastA;
    gl_FragColor = vec4(finalColor.rgb, alpha);
}
