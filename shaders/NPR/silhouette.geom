#version 430

layout (triangles_adjacency) in;
layout (line_strip, max_vertices=36) out;

uniform bool is_sketching;
uniform vec3 light_position;
uniform sampler2D noise_texture;

in VS_OUT {
    vec3 vertex;
    vec2 texcoord;
} gs_in[]; 


void EmitLine(vec4 start_pos, vec4 end_pos)
{
    gl_Position = start_pos;
    EmitVertex();

    gl_Position = end_pos;
    EmitVertex();

    EndPrimitive();
}

void EmitDisplacedLines(int start_index, int end_index)
{   
    for (int i = 0; i < 3; i++)
    {
        vec4 start_point = gl_in[start_index].gl_Position;
        vec4 end_point = gl_in[end_index].gl_Position;

        float offset = clamp(texture(noise_texture, gs_in[start_index].texcoord).r + texture(noise_texture, gs_in[end_index].texcoord).r, -0.5, 0.5);
        vec4 distance_vec = (end_point - start_point)/ (2.0 + offset);

        int rand_int = int(clamp(texture(noise_texture, gs_in[start_index].texcoord).g, 0, 10));

        float direction = ((i % 2 == 0) ? 1.0 : -1.0) * i * 1.2;

        vec4 mid_point = start_point + distance_vec + texture(noise_texture, gs_in[(end_index + i + rand_int) % 5].texcoord).rgba * direction;

        if (rand_int % 2 == 0)
            start_point += texture(noise_texture, gs_in[(end_index + i + rand_int) % 5].texcoord).rgba * direction;
        else    
            end_point += texture(noise_texture, gs_in[(end_index + i + rand_int) % 5].texcoord).rgba * direction;

        EmitLine(start_point, mid_point);
        EmitLine(mid_point, end_point);
    }
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
    vec3 light_direction = normalize(light_position - gs_in[0].vertex);

    if (dot(normal, light_direction) > 0.00001) {

        normal = cross(e3, e1);

        if (dot(normal, light_direction) <= 0.0)
            if (is_sketching)
                EmitDisplacedLines(0, 2);
            else
                EmitLine(gl_in[0].gl_Position, gl_in[2].gl_Position);

        normal = cross(e4, e5);
        light_direction = light_position - gs_in[2].vertex;

        if (dot(normal, light_direction) <= 0)
            if (is_sketching)
                EmitDisplacedLines(2, 4);
            else
                EmitLine(gl_in[2].gl_Position, gl_in[4].gl_Position);

        normal = cross(e2,e6);
        light_direction = light_position - gs_in[4].vertex;

        if (dot(normal, light_direction) <= 0)
            if (is_sketching)
                EmitDisplacedLines(4, 0);
            else
                EmitLine(gl_in[4].gl_Position, gl_in[0].gl_Position);
    }
} 
