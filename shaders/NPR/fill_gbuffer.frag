#version 410

uniform bool has_normals_texture;
uniform bool has_opacity_texture;
uniform sampler2D normals_texture;
uniform sampler2D opacity_texture;
uniform mat4 normal_model_to_world;

in VS_OUT {
	vec3 normal;
	vec2 texcoord;
	vec3 tangent;
	vec3 binormal;
} fs_in;

layout (location = 0) out vec4 geometry_normal;


void main()
{
	if (has_opacity_texture && texture(opacity_texture, fs_in.texcoord).r < 1.0)
		discard;

	// Worldspace normal
	geometry_normal.xyz = fs_in.normal.xyz * 0.5 + 0.5;
	if (has_normals_texture)
	{	
		mat3 tbn = mat3(fs_in.tangent, fs_in.binormal, fs_in.normal);
		vec3 normal = texture(normals_texture, fs_in.texcoord).rgb * 2.0 - 1.0;
		geometry_normal.xyz = normalize((normal_model_to_world * vec4(tbn * normal, 0.0)).xyz) * 0.5 + 0.5;
	}
}
