#version 410

uniform vec3 light_position;

in VS_OUT {
	vec3 vertex;
	vec3 normal;
} fs_in;

layout (location = 0) out vec4 geometry_diffuse;


void main()
{
	vec3 L = normalize(light_position - fs_in.vertex);
	geometry_diffuse = vec4(1.0) * clamp(dot(normalize(fs_in.normal), L), 0.0, 1.0);
}
