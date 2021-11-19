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
layout (location = 2) out vec4 geometry_normal;


void main()
{
	vec2 tex_coords = gl_FragCoord.xy * inverse_screen_resolution.xy;    // this should be [0-1]
	float depth = texture(depth_texture, tex_coords).z; 				 // this should be [0-1]
	vec4 proj_pos = vec4(tex_coords, depth, gl_FragCoord.w) * 2.0 - 1.0; // this should be [-1, 1]
	vec3 world_pos = (proj_pos * camera.view_projection_inverse).xyz / gl_FragCoord.w;

	vec3 normal_vec = geometry_normal.xyz * 2.0 - 1.0; // this should be [-1, 1]
	vec3 light_vec = normalize(light_position - world_pos);
	vec3 view_vec = normalize(camera_position - world_pos);
	vec3 reflection_vec = normalize(reflect(-light_vec, normal_vec));

	vec3 diffuse = max(dot(normal_vec, light_vec), 0.0)  * light_color;
	vec3 specular = pow(max(dot(reflection_vec, view_vec), 0.0), light_intensity)  * light_color;

	light_diffuse_contribution  = vec4(diffuse, 1.0); // funky behaviour
	light_specular_contribution = vec4(specular, 1.0); // doesn't seem to work at all
}
