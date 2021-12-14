#version 430

layout (triangles_adjacency) in;
layout (line_strip, max_vertices=6) out;

uniform vec3 light_position;
uniform sampler2D noise_texture;

in VS_OUT {
    vec3 vertex;
} gs_in[]; 


void EmitLine(int start, int end)
{
	vec2 disturbance1=texture(noise_texture,gs_in[start].vertex.xy).rg;
	vec2 disturbance2=texture(noise_texture,gs_in[end].vertex.xy).gb;

	vec3 length=(gs_in[start].vertex-gs_in[end].vertex)*0.2;
	vec3 overlap=length*0.2;

    gl_Position = gl_in[start].gl_Position+vec4(disturbance1,0.0f,0.0f);
    EmitVertex();

    gl_Position = gl_in[end].gl_Position+vec4(disturbance2,0.0f,0.0f)+vec4(overlap,0.0f);
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
    vec3 light_direction = normalize(light_position - gs_in[0].vertex);

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
