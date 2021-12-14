#version 430

uniform sampler2D diffuse_texture;
uniform sampler2D silhouette_texture;


in VS_OUT {
	vec2 texcoord;
} fs_in;

out vec4 frag_color;

void main()
{
	vec3 diffuse  = texture(diffuse_texture,  fs_in.texcoord).rgb;
	vec3 silhouette  = texture(silhouette_texture,  fs_in.texcoord).rgb;

	vec3 final_color = diffuse;

	if (length(silhouette) < 0.9)
		final_color = silhouette;

	frag_color =  vec4(final_color, 1.0);
}
