#version 430

layout (triangles_adjacency) in;
layout (line_strip, max_vertices=6) out;

uniform vec3 light_position;


in vec3 vertex[];


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
    vec3 e1 = vertex[2] - vertex[0];
    vec3 e2 = vertex[4] - vertex[0];
    vec3 e3 = vertex[1] - vertex[0];
    vec3 e4 = vertex[3] - vertex[2];
    vec3 e5 = vertex[4] - vertex[2];
    vec3 e6 = vertex[5] - vertex[0];

    vec3 normal = cross(e1,e2);
    vec3 light_direction = light_position - vertex[0];

    if (dot(normal, light_direction) > 0.00001) {

        normal = cross(e3,e1);

        if (dot(normal, light_direction) <= 0) {
            EmitLine(0, 2);
        }

        normal = cross(e4,e5);
        light_direction = light_position - vertex[2];

        if (dot(normal, light_direction) <=0) {
            EmitLine(2, 4);
        }

        normal = cross(e2,e6);
        light_direction = light_position - vertex[4];

        if (dot(normal, light_direction) <= 0) {
            EmitLine(4, 0);
        }
    }
} 
