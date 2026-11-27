#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 panelSize;
    vec4 tint;
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

    float refractionWeight = smoothstep(-22.0, -2.0, edge);
    vec2 center = qt_TexCoord0 - normal * (refraction * refractionWeight) / panelSize;
    vec2 offset = vec2(blurRadius, blurRadius) / panelSize;
    vec3 background = texture(source, center).rgb * 0.20;
    background += texture(source, center + vec2(offset.x, 0.0)).rgb * 0.11;
    background += texture(source, center - vec2(offset.x, 0.0)).rgb * 0.11;
    background += texture(source, center + vec2(0.0, offset.y)).rgb * 0.11;
    background += texture(source, center - vec2(0.0, offset.y)).rgb * 0.11;
    background += texture(source, center + offset).rgb * 0.09;
    background += texture(source, center - offset).rgb * 0.09;
    background += texture(source, center + vec2(offset.x, -offset.y)).rgb * 0.09;
    background += texture(source, center + vec2(-offset.x, offset.y)).rgb * 0.09;

    vec3 material = mix(background, tint.rgb, tintStrength);
    float topLight = max(0.0, dot(normal, vec2(-0.35, -0.93)));
    float sheen = topLight * refractionWeight * refractionWeight
        * (refraction > 0.0 ? 0.26 : 0.14);
    material = mix(material, vec3(0.91, 0.96, 1.0), sheen);
    fragColor = vec4(material, 1.0) * coverage * qt_Opacity;
}
