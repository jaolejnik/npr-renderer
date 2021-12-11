#version 410

uniform vec3 light_position;
uniform sampler2D diffuse_texture;
uniform bool has_diffuse_texture;

in VS_OUT {
	vec3 vertex;
	vec2 texcoord;
	vec3 normal;
	vec3 tangent;
	vec3 binormal;
} fs_in;

out vec4 frag_color;


void main()
{
	vec3 L = normalize(light_position - fs_in.vertex);
	vec3 diffuse = (has_diffuse_texture ? vec3(texture(diffuse_texture, fs_in.texcoord)) : vec3(1.0)) * max(dot(normalize(fs_in.normal), L), 0.0);
	
	frag_color = vec4(diffuse, 1.0);
}
