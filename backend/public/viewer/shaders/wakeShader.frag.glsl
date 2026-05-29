#ifdef GL_ES
    precision highp float;
#endif

varying vec2 vUV;

uniform sampler2D wakeTexture;
uniform float time;
uniform float wakeAlpha;

void main(void) {
    // vUV.y: 0 = just behind the stern, 1 = far tail.
    // Scroll the foam away from the boat over time for a "moving water" look.
    vec2 uvA = vec2(vUV.x, vUV.y * 2.2 - time * 0.18);
    vec2 uvB = vec2(vUV.x * 1.7 + 0.37, vUV.y * 3.4 - time * 0.31);
    float foam = texture2D(wakeTexture, uvA).r * 0.65
               + texture2D(wakeTexture, uvB).r * 0.55;
    foam = clamp(foam, 0.0, 1.0);

    // Fade out toward the tail, and soften the left/right edges of the trail.
    float lenFade = 1.0 - smoothstep(0.05, 1.0, vUV.y);
    float edgeFade = smoothstep(0.0, 0.14, vUV.x) * (1.0 - smoothstep(0.86, 1.0, vUV.x));

    float a = foam * lenFade * edgeFade * wakeAlpha;
    if (a < 0.02) discard;

    gl_FragColor = vec4(vec3(0.95, 0.98, 1.0), a);
}
