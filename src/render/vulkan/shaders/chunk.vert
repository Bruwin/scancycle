#version 450
// Voxel vertex stage. Vertices are the mesher's 8-byte packed form pulled out of
// a single storage buffer, so there is no per-chunk vertex buffer and one draw
// call covers the world.
//
// Each indirect draw carries its chunk index in firstInstance, and the vertex
// shader reads that chunk's origin. That is why the draw needs
// shaderDrawParameters and drawIndirectFirstInstance enabled.

layout(location = 0) flat out vec3 v_normal;
layout(location = 1) flat out uint v_material;
layout(location = 2) out float v_ao;
layout(location = 3) out vec3 v_world;

layout(set = 0, binding = 0) uniform FrameBlock {
    mat4 view_projection;
    vec4 camera_position;
    vec4 frustum_planes[6];
    uint chunk_count;
    uint max_chunks;
    float max_distance;
    float pad0;
} frame;

// uvec2 per vertex: bytes 0..3 are x, y, z, face; bytes 4..7 are ao, pad,
// then the 16-bit material. Little-endian, which every supported target is.
layout(set = 0, binding = 1) readonly buffer VertexBuffer { uvec2 vertices[]; };

// xyz = chunk origin in metres (chunk-local coords are 0..32), w = bound radius.
layout(set = 0, binding = 2) readonly buffer ChunkBuffer { vec4 chunks[]; };

const vec3 kFaceNormals[6] = vec3[6](
    vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0), vec3(0.0, -1.0, 0.0),
    vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, -1.0));

void main() {
    uvec2 packed = vertices[gl_VertexIndex];

    uint x = packed.x & 0xFFu;
    uint y = (packed.x >> 8) & 0xFFu;
    uint z = (packed.x >> 16) & 0xFFu;
    uint face = (packed.x >> 24) & 0xFFu;

    uint ao = packed.y & 0xFFu;
    v_material = packed.y >> 16;

    vec3 origin = chunks[gl_InstanceIndex].xyz;
    vec3 world = origin + vec3(float(x), float(y), float(z));

    v_normal = kFaceNormals[face];
    v_ao = float(ao) / 255.0;
    v_world = world;

    gl_Position = frame.view_projection * vec4(world, 1.0);
}
