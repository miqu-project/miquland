precision mediump float;

varying mediump vec2 v_texcoord;
uniform sampler2D tex;

uniform float radius;
uniform vec2 halfpixel;
uniform vec2 min_uv;
uniform vec2 max_uv;

void main() {
    vec2 uv = v_texcoord * 2.0;

    vec4 sum = texture2D(tex, clamp(uv, min_uv, max_uv)) * 4.0;
    sum += texture2D(tex, clamp(uv - halfpixel.xy * radius, min_uv, max_uv));
    sum += texture2D(tex, clamp(uv + halfpixel.xy * radius, min_uv, max_uv));
    sum += texture2D(tex, clamp(uv + vec2(halfpixel.x, -halfpixel.y) * radius, min_uv, max_uv));
    sum += texture2D(tex, clamp(uv - vec2(halfpixel.x, -halfpixel.y) * radius, min_uv, max_uv));

    gl_FragColor = sum / 8.0;
}
