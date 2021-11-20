#version 410

struct ViewProjTransforms
{
	mat4 view_projection;
	mat4 view_projection_inverse;
};

layout (std140) uniform CameraViewProjTransforms
{
	ViewProjTransforms camera;
};

layout (std140) uniform LightViewProjTransforms
{
	ViewProjTransforms lights[4];
};

uniform int light_index;

uniform sampler2D depth_texture;
uniform sampler2D normal_texture;
uniform sampler2DShadow shadow_texture;

uniform vec2 inverse_screen_resolution;

uniform vec3 camera_position;

uniform vec3 light_color;
uniform vec3 light_position;
uniform vec3 light_direction;
uniform float light_intensity;
uniform float light_angle_falloff;

uniform vec2 shadowmap_texel_size;

layout (location = 0) out vec4 light_diffuse_contribution;
layout (location = 1) out vec4 light_specular_contribution;


void main()
{	
	vec2 tex_coords = gl_FragCoord.xy * inverse_screen_resolution;
	float depth = texture(depth_texture, tex_coords).r;

	vec4 proj_pos = vec4(tex_coords, depth, 1.0) * 2.0 - 1.0;
	vec4 world_pos = camera.view_projection_inverse * proj_pos;
	world_pos /= world_pos.w; 


	vec3 light = normalize(light_position - world_pos.xyz);
	vec3 normal = texture(normal_texture, tex_coords).xyz * 2.0 - 1.0;
	vec3 view = normalize(camera_position - world_pos.xyz);
	vec3 reflection = normalize(reflect(-light_direction, normal));

	float light_distance = length(light_position - world_pos.xyz);
	float light_angle = dot(light_direction, light);

	vec3 diffuse = vec3(0.0);
	vec3 specular = vec3(0.0);
	
	if (light_angle < light_angle_falloff)
	{
		diffuse += max(dot(normal, light_direction), 0.0) * light_color * light_intensity / (light_distance * light_distance);
		 // TODO find a proper value for shininess
		specular += pow(max(dot(reflection, view), 0.0), 1000) * light_color * light_intensity / (light_distance * light_distance);
	}

	light_diffuse_contribution  = vec4(diffuse, 1.0); 
	light_specular_contribution = vec4(specular, 1.0);
}
