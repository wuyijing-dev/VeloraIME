#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 panelSize;
    vec4 tint;
    vec4 lensLight;
    float tintStrength;
    float cornerRadius;
    float refraction;
    float blurRadius;
};

layout(binding = 1) uniform sampler2D source;

void main()
{
    vec2 position = (qt_TexCoord0 - 0.5) * panelSize;
    vec2 corner = abs(position) - (panelSize * 0.5 - cornerRadius);
    vec2 outer = max(corner, 0.0);
    float edge = length(outer) + min(max(corner.x, corner.y), 0.0) - cornerRadius;
    float coverage = 1.0 - smoothstep(-0.75, 0.75, edge);

    vec2 normal;
    if (dot(outer, outer) > 0.0001) {
        normal = normalize(outer) * sign(position);
    } else {
        normal = corner.x > corner.y ? vec2(sign(position.x), 0.0)
                                      : vec2(0.0, sign(position.y));
    }

    float edgeLens = smoothstep(-16.0, -1.0, edge);
    vec2 center = qt_TexCoord0 - normal * (refraction * edgeLens * edgeLens) / panelSize;
    vec2 offset = vec2(blurRadius) / panelSize;
    vec2 diagonal = offset * 0.72;
    vec3 background = texture(source, center).rgb * 0.24;
    background += texture(source, center + vec2(offset.x, 0.0)).rgb * 0.12;
    background += texture(source, center - vec2(offset.x, 0.0)).rgb * 0.12;
    background += texture(source, center + vec2(0.0, offset.y)).rgb * 0.12;
    background += texture(source, center - vec2(0.0, offset.y)).rgb * 0.12;
    background += texture(source, center + diagonal).rgb * 0.07;
    background += texture(source, center - diagonal).rgb * 0.07;
    background += texture(source, center + vec2(diagonal.x, -diagonal.y)).rgb * 0.07;
    background += texture(source, center + vec2(-diagonal.x, diagonal.y)).rgb * 0.07;

    float luminance = dot(background, vec3(0.2126, 0.7152, 0.0722));
    float darkMode = 1.0 - step(0.5, tint.r);
    float correction = mix(1.0 - luminance, luminance, darkMode);
    float density = tintStrength + 0.17 * correction;
    vec3 material = mix(mix(vec3(luminance), background, 0.72), tint.rgb, density);

    // The curved edge transmits more of its surroundings and catches light
    // differently around the silhouette. The center stays diffuse for labels.
    float innerRim = smoothstep(-10.0, -2.4, edge) * (1.0 - smoothstep(-0.8, 0.6, edge));
    material = mix(material, background, innerRim * (refraction > 0.0 ? 0.20 : 0.07));
    float illuminated = max(0.0, dot(normal, vec2(-0.49, -0.87)));
    float shaded = max(0.0, dot(normal, vec2(0.38, 0.92)));
    float reflection = pow(illuminated, 2.6) * innerRim;
    float topCorner = 1.0 - smoothstep(0.0, 0.44, length(qt_TexCoord0 - vec2(0.89, 0.05)));
    float bottomCorner = 1.0 - smoothstep(0.0, 0.42, length(qt_TexCoord0 - vec2(0.11, 0.98)));
    float edgePrism = (topCorner * 0.58 + bottomCorner * 0.38) * smoothstep(-17.0, 0.0, edge);
    float border = smoothstep(-1.5, -0.2, edge);
    material *= 1.0 - shaded * (0.08 * innerRim + 0.06 * border);
    material = mix(material, lensLight.rgb,
                   clamp(edgePrism + reflection * mix(0.34, 0.30, darkMode) + illuminated * border * 0.10, 0.0, 0.83));
    fragColor = vec4(material, 1.0) * coverage * qt_Opacity;
}
