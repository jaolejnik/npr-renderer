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
uniform float light_angle_outer_falloff;

layout (location = 0) out vec4 light_diffuse_contribution;
layout (location = 1) out vec4 light_specular_contribution;


void main()
{	
	vec2 tex_coords = gl_FragCoord.xy * inverse_screen_resolution;
	float depth = texture(depth_texture, tex_coords).r;

	vec4 proj_pos = vec4(tex_coords, depth, 1.0) * 2.0 - 1.0;
	vec4 world_pos = camera.view_projection_inverse * proj_pos;
	world_pos /= world_pos.w; 


	vec3 L = normalize(light_position - world_pos.xyz);
	vec3 N = texture(normal_texture, tex_coords).xyz * 2.0 - 1.0;
	vec3 V = normalize(camera_position - world_pos.xyz);
	vec3 R = normalize(reflect(-L, N));

	float light_distance = length(light_position - world_pos.xyz);
	float linear_falloff = 1.0 / (light_distance * light_distance);

	float theta = dot(light_direction, -L);
	float epsilon =  cos(light_angle_outer_falloff) - cos(light_angle_falloff) ;
	float angular_falloff = clamp((theta - cos(light_angle_falloff)) / epsilon, 0.0, 1.0);

	float light_total_intensity =  light_intensity * angular_falloff * linear_falloff;

	vec3 diffuse = max(dot(N, L), 0.0) * light_color * light_total_intensity;
	vec3 specular = pow(max(dot(R, V), 0.0), 50.0) * light_color * light_total_intensity;

	//Computing the projected pixel coordinates from the shadow map camera with perspective divide.
	vec4 pixel_position_in_lightSpace = lights[light_index].view_projection * world_pos;
	pixel_position_in_lightSpace /= pixel_position_in_lightSpace.w;

	//Converting to texture coordinates.
	pixel_position_in_lightSpace = pixel_position_in_lightSpace * 0.5f + 0.5f;

	//Retrieve the depth from the shadow map camera.
	pixel_position_in_lightSpace.z -= 0.0002;
	float weight_pixel_distance_SM = 0.0;
	vec2 shadowmap_texel_size = 1.0 / textureSize(shadow_texture, 0);

	for(int i = -1; i < 2; i++)
	{
		pixel_position_in_lightSpace.x = pixel_position_in_lightSpace.x + shadowmap_texel_size.x * i;

		for (int j = -1; j < 2; j++) 
		{
			pixel_position_in_lightSpace.y = pixel_position_in_lightSpace.y + shadowmap_texel_size.y * j;
			float other_pixel_distance_SM = texture(shadow_texture, pixel_position_in_lightSpace.xyz);
			weight_pixel_distance_SM += other_pixel_distance_SM;
		}
	}	

	light_diffuse_contribution  = vec4(diffuse * weight_pixel_distance_SM / 9.0, 1.0); 
	light_specular_contribution = vec4(specular * weight_pixel_distance_SM / 9.0, 1.0);
}
