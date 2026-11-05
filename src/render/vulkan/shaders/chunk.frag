#version 450
// Voxel fragment stage. Materials are procedural by id: the repository ships no
// texture files, and a photo-eye has to stay readable at twenty metres, so the
// palette is chosen for contrast rather than realism.

layout(location = 0) flat in vec3 v_normal;
layout(location = 1) flat in uint v_material;
layout(location = 2) in float v_ao;
layout(location = 3) in vec3 v_world;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform FrameBlock {
    mat4 view_projection;
    vec4 camera_position;
    vec4 frustum_planes[6];
    uint chunk_count;
    uint max_chunks;
    float max_distance;
    float pad0;
} frame;

// Industrial palette, indexed by material id from the material library. Kept in
// one place so a new material is a line here and a line there, nothing more.
vec3 material_color(uint id) {
    uint index = id % 10u;
    if (index == 0u) return vec3(0.55, 0.56, 0.58);  // concrete
    if (index == 1u) return vec3(0.22, 0.42, 0.68);  // machine blue
    if (index == 2u) return vec3(0.80, 0.62, 0.16);  // safety yellow
    if (index == 3u) return vec3(0.30, 0.31, 0.33);  // steel
    if (index == 4u) return vec3(0.72, 0.24, 0.18);  // e-stop red
    if (index == 5u) return vec3(0.26, 0.52, 0.30);  // conveyor green
    if (index == 6u) return vec3(0.86, 0.86, 0.84);  // painted white
    if (index == 7u) return vec3(0.16, 0.17, 0.19);  // rubber
    if (index == 8u) return vec3(0.45, 0.30, 0.20);  // timber
    return vec3(0.36, 0.38, 0.42);                   // default
}

void main() {
    vec3 base = material_color(v_material);

    // A key light over the left shoulder plus a dim sky term, so faces stay
    // distinguishable without a lighting pass.
    vec3 light_dir = normalize(vec3(-0.4, 0.9, 0.3));
    float lambert = max(dot(normalize(v_normal), light_dir), 0.0);
    float lighting = 0.35 + 0.65 * lambert;

    vec3 color = base * lighting * mix(0.55, 1.0, v_ao);

    // Distance fade towards the horizon so a large factory reads as depth rather
    // than a wall of geometry.
    float distance = length(v_world - frame.camera_position.xyz);
    float fade = clamp(distance / max(frame.max_distance, 1.0), 0.0, 1.0);
    color = mix(color, vec3(0.58, 0.66, 0.76), fade * 0.55);

    out_color = vec4(color, 1.0);
}
