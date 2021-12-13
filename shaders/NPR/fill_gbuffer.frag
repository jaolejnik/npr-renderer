#version 430

uniform vec3 light_position;
uniform vec3 camera_position;
uniform float thickness;
uniform float ring; // not sure what that will be 
uniform sampler2D noise_texture;

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

float diagonal_noised(float sample_scale, float thickness, float direction, vec2 noise) 
{
	vec2 pixel = floor(vec2(gl_FragCoord));
	float a = 1.0;
	float stroke_noise = cos(length(pixel) / noise.x) * noise.y;
	float stroke_direction = pixel.x - pixel.y * direction;
	float b = mod(stroke_direction  + stroke_noise, thickness);
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
	vec3 c = vec3(1.0);
	float b = dot(N, L);
	if (b > 0.0) 
	{
		color += vec3(1.0) * b * c;  
		vec3 R = reflect(-L, N);
		float s = pow(max(dot(R, V), 0.0), 25.0);
		s = smoothstep(0.1, 1.0, s);
		color += s;
	}

	return color;
}

void main()
{
	vec3 color = vec3(0.4, 0.3, 0.6);
	vec3 V = normalize(camera_position - fs_in.vertex);
	vec3 L = normalize(light_position - fs_in.vertex);
	vec3 shaded_color = shade(L, V, fs_in.normal, color);

	float scale = shaded_color.r;
	vec3 noise = texture(noise_texture, fs_in.texcoord).rgb;
	float hatch = circles(scale, 5);
	// float hatch = diagonal(scale, 5, 1.0) * diagonal(scale, 5, -1.0);
	// float hatch = diagonal_noised(scale, 20, 1.0 + noise.g, noise.rb) * diagonal_noised(scale, 20, -1.0 + noise.r, noise.bg);

	hatch = clamp(hatch, 0.1, 1.0);

	frag_color = vec4(vec3(hatch), 1.0);
}
