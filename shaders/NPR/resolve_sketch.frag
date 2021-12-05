#version 410

uniform sampler2D diffuse_texture;


in VS_OUT {
	vec2 texcoord;
} fs_in;

out vec4 frag_color;

void main()
{
	vec3 diffuse  = texture(diffuse_texture,  fs_in.texcoord).rgb;

	// TODO add other elements of the pipeline

	frag_color =  vec4(diffuse, 1.0);
}
