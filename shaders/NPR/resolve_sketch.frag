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

	if ((dot(silhouette, vec3(1.0)) / 3) < 0.9)
		diffuse = silhouette;

	// TODO add other elements of the pipeline
					// !TEMPORARY CHANGE
	frag_color =  vec4(silhouette, 1.0);
	// frag_color =  vec4(diffuse, 1.0);
}
