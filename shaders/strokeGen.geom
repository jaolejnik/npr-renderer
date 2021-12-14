#version 430


layout (triangles_adjacency) in;
layout (line_strip, max_vertices=6) out;

uniform vec3 light_position;
// uniform vec2 inverse_screen_resolution;
// uniform sampler2D depth_texture;

in VS_OUT {
    vec3 vertex;
} gs_in[]; 


void EmitLine(int start, int end)
{
    gl_Position = gl_in[start].gl_Position;
    EmitVertex();

    gl_Position = gl_in[end].gl_Position;
    EmitVertex();

    EndPrimitive();
}

void main()
{
    vec3 e1 = gs_in[2].vertex - gs_in[0].vertex;
    vec3 e2 = gs_in[4].vertex - gs_in[0].vertex;
    vec3 e3 = gs_in[1].vertex - gs_in[0].vertex;
    vec3 e4 = gs_in[3].vertex - gs_in[2].vertex;
    vec3 e5 = gs_in[4].vertex - gs_in[2].vertex;
    vec3 e6 = gs_in[5].vertex - gs_in[0].vertex;

    vec3 normal = cross(e1, e2);
    vec3 light_direction = light_position - gs_in[0].vertex;

    if (dot(normal, light_direction) > 0.00001) {

        normal = cross(e3, e1);

        if (dot(normal, light_direction) <= 0)
            EmitLine(0, 2);

        normal = cross(e4, e5);
        light_direction = light_position - gs_in[2].vertex;

        if (dot(normal, light_direction) <= 0)
            EmitLine(2, 4);

        normal = cross(e2,e6);
        light_direction = light_position - gs_in[4].vertex;

        if (dot(normal, light_direction) <= 0)
            EmitLine(4, 0);
    }
} 
