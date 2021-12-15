#version 430

uniform vec3 light_position;
uniform vec3 camera_position;
uniform vec3 diffuse_color;
uniform float thickness;
uniform bool is_sketching;

in VS_OUT {
	vec3 vertex;
	vec2 texcoord;
	vec3 normal;
	vec3 tangent;
	vec3 binormal;
} fs_in;

out vec4 frag_color;

float balance(float sample_scale, float weight)
{
	if (weight < 1.0)
		weight = sample_scale + weight;

	return clamp(pow(weight, 5.0), 0.0, 1.0);
}

float diagonal(float sample_scale, float thickness, float direction) 
{
	vec2 pixel = floor(vec2(gl_FragCoord));
	float a = 1.0;
	float stroke_direction = pixel.x - pixel.y * direction;
	float b = mod(stroke_direction, thickness);
	float c = thickness / 2.0;
	if (b < thickness) 
		a = abs(b - c) / c;

	return balance(sample_scale, a);  
}

float circles(float sample_scale, float thickness) {
  vec2 pixel = floor(vec2(gl_FragCoord));
  float b = thickness / 2.0;
  if (mod((pixel.y), thickness * 2.0) > thickness)
    pixel.x += b;
  pixel = mod(pixel, vec2(thickness));
  float a = distance(pixel, vec2(b)) / (thickness * 0.65);
  return balance(sample_scale, a);  
}

vec3 shade(vec3 L, vec3 V,  vec3 N, vec3 color) 
{
	
	float diffuse = max(dot(N, L), 0.0);
	color += diffuse;  
	vec3 R = reflect(-L, N);
	float specular = pow(max(dot(R, V), 0.0), 2.0);
	specular = smoothstep(0.0, 1.0, specular);
	color += specular;

	return color;
}

void main()
{
	vec3 L = normalize(light_position - fs_in.vertex);

	if(is_sketching)
		frag_color = vec4(1.0) * clamp(dot(normalize(fs_in.normal), L), 0.0, 1.0);
	else
	{
		vec3 color = diffuse_color;
		vec3 V = normalize(camera_position - fs_in.vertex);
		vec3 shaded_color = shade(L, V, fs_in.normal, color);

		float scale = min(min(shaded_color.r, shaded_color.g), shaded_color.b);
		float hatch = circles(scale, thickness);

		frag_color = vec4(color * hatch, 1.0);
	}
}
