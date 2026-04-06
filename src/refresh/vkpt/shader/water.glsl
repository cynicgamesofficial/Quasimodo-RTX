/*
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

const float WATER_TAU = 6.28318;
const int WATER_NUM_WAVES = 6;
// Golden-angle spacing avoids rational interference axes, while the 420..52
// wavelength spread keeps slope energy broad without overdriving the short waves.
const vec4 WATER_WAVES[WATER_NUM_WAVES] = vec4[WATER_NUM_WAVES](
	vec4( 0.981,  0.196, 2.30, 420.0),
	vec4(-0.855,  0.518, 1.55, 260.0),
	vec4( 0.281, -0.960, 1.00, 160.0),
	vec4( 0.441,  0.897, 0.62, 100.0),
	vec4(-0.931, -0.364, 0.36,  68.0),
	vec4( 0.933, -0.361, 0.22,  52.0)
);

float get_water_wave_length(float wavelength)
{
	return wavelength * max(global_ubo.pt_water_wave_scale, 0.01);
}

float get_water_wave_phase(float wavelength)
{
	return max(global_ubo.pt_water_wave_speed, 0.0) * sqrt(9.8 * WATER_TAU / wavelength);
}

vec3 get_water_current_analytic_normal(vec2 p)
{
	const float WAVE_SPEED_SCALE = 24;
	float wave_steepness = global_ubo.pt_water_wave_steepness;
	float dx = 0.0;
	float dy = 0.0;

	for(int i = 0; i < WATER_NUM_WAVES; i++)
	{
		vec2 D = normalize(WATER_WAVES[i].xy);
		float A = WATER_WAVES[i].z;
		float L = get_water_wave_length(WATER_WAVES[i].w);
		float w = WATER_TAU / L;
		float phi = w * WAVE_SPEED_SCALE * max(global_ubo.pt_water_wave_speed, 0.0);
		float theta = dot(D, p.xy) * w + global_ubo.time * phi;
		float s = sin(theta);
		float c = cos(theta);
		float wave_derivative = c;
		if(wave_steepness > 1.0)
		{
			float h = pow((s + 1.0) * 0.5, max(1.0, wave_steepness - 1.0));
			wave_derivative = wave_steepness * h * c;
		}
		dx += A * w * D.x * wave_derivative;
		dy += A * w * D.y * wave_derivative;
	}

	return normalize(vec3(-dx, -dy, 1.0)).xzy;
}

vec3 get_water_reference_wave_normal(vec2 p, int water_mode)
{
	float dx = 0.0;
	float dy = 0.0;
	float vertical = 0.0;
	float wave_steepness = max(global_ubo.pt_water_wave_steepness, 1.0);

	for(int i = 0; i < WATER_NUM_WAVES; i++)
	{
		vec2 D = normalize(WATER_WAVES[i].xy);
		float A = WATER_WAVES[i].z;
		float L = get_water_wave_length(WATER_WAVES[i].w);
		float w = WATER_TAU / L;
		float theta = dot(D, p.xy) * w + global_ubo.time * get_water_wave_phase(L);
		float s = sin(theta);
		float c = cos(theta);

		if(water_mode == 2)
		{
			dx += A * w * D.x * c;
			dy += A * w * D.y * c;
		}
		else if(water_mode == 3)
		{
			float h = pow((s + 1.0) * 0.5, max(1.0, wave_steepness - 1.0));
			float derivative = wave_steepness * h * c;
			dx += A * w * D.x * derivative;
			dy += A * w * D.y * derivative;
		}
		else
		{
			float wa = w * A;
			float q = wave_steepness / float(WATER_NUM_WAVES);
			dx += D.x * wa * c;
			dy += D.y * wa * c;
			vertical += q * wa * s;
		}
	}

	if(water_mode == 4)
		return normalize(vec3(-dx, -dy, 1.0 - vertical)).xzy;

	return normalize(vec3(-dx, -dy, 1.0)).xzy;
}

vec3 get_water_fbm_normal(vec2 p)
{
	const int FBM_WAVE_COUNT = 12;
	const float FREQUENCY_MULT = 1.18;
	const float AMPLITUDE_MULT = 0.82;
	const float SPEED_RAMP = 1.07;
	const float DRAG = 1.0;
	const float MAX_PEAK = 1.0;
	const float PEAK_OFFSET = 1.0;
	const float SEED_ITER = 1253.2131;

	float frequency = 0.012 / max(global_ubo.pt_water_wave_scale, 0.01);
	float amplitude = 1.0;
	float speed = 2.0 * max(global_ubo.pt_water_wave_speed, 0.0);
	float seed = 0.0;
	float amplitude_sum = 0.0;
	vec2 sample_pos = p;
	vec2 gradient = vec2(0.0);

	for(int wave_index = 0; wave_index < FBM_WAVE_COUNT; ++wave_index)
	{
		vec2 direction = normalize(vec2(cos(seed), sin(seed)));
		float x = dot(direction, sample_pos) * frequency + global_ubo.time * speed;
		float wave = amplitude * exp(MAX_PEAK * sin(x) - PEAK_OFFSET);
		vec2 derivative = frequency * direction * (MAX_PEAK * wave * cos(x));

		sample_pos += -derivative * amplitude * DRAG;
		gradient += derivative;
		amplitude_sum += amplitude;

		frequency *= FREQUENCY_MULT;
		amplitude *= AMPLITUDE_MULT;
		speed *= SPEED_RAMP;
		seed += SEED_ITER;
	}

	gradient /= max(amplitude_sum, 0.0001);
	return normalize(vec3(-gradient.x, -gradient.y, 1.0)).xzy;
}

vec3 get_water_normal(uint material_id, vec3 geo_normal, vec3 tangent, vec3 position, bool local_space)
{
	// Add flow
	if((material_id & MATERIAL_FLAG_FLOWING) != 0)
	{
		position -= tangent * global_ubo.time * 32;
	}	

	// Remove the sign from the normal to make water simulation uniform,
	// regardless of which side we're looking at the surface from.
	// This is necessary to have caustics motion match the waves.
	vec3 unsigned_geo_normal = abs(geo_normal);

	// Construct a basis around the normal, get object-local 2D position.
	mat3 basis = construct_ONB_frisvad(unsigned_geo_normal);
	vec2 p = vec2(dot(position, basis[0]), dot(position, basis[2]));

	vec3 n;
	int water_mode = int(global_ubo.pt_water_fbm + 0.5);
	if(water_mode != 0)
	{
		if(water_mode == 1)
			n = get_water_current_analytic_normal(p);
		else if(water_mode == 2 || water_mode == 3 || water_mode == 4)
			n = get_water_reference_wave_normal(p, water_mode);
		else if(water_mode == 5)
			n = get_water_fbm_normal(p);
		else
			n = get_water_current_analytic_normal(p);
	}
	else
	{
		// Sample the texture and add a few instances of noise.

		const float speed = 2.5;

		vec2 uv1 = p.xy * 0.006 + global_ubo.time * vec2(0.01, 0.02) * speed;
		vec3 a = global_textureLod(global_ubo.water_normal_texture, uv1, 0).xyz;
		a.xy = a.xy * 2 - vec2(1);
		a.xy *= 0.3;

		vec2 uv2 = p.xy * 0.003 + global_ubo.time * vec2(0.013, 0.014) * speed;
		vec3 b = global_textureLod(global_ubo.water_normal_texture, uv2, 0).xyz;
		b.xy = b.xy * 2 - vec2(1);
		b.xy *= 0.5;

		vec2 uv3 = p.xy * 0.0061 + global_ubo.time * vec2(-0.01, -0.02) * speed;
		vec3 c = global_textureLod(global_ubo.water_normal_texture, uv3, 0).xyz;
		c.xy = c.xy * 2 - vec2(1);
		c.xy *= 0.3;

		n = normalize(a + b + c).xzy;
	}

	if(local_space)
		return n;

	// Back into world space
	n = basis * n;

	// Restore the sign
	if(geo_normal.x < 0) n.x = -n.x;
	if(geo_normal.y < 0) n.y = -n.y;
	if(geo_normal.z < 0) n.z = -n.z;

	return n;
}

vec3 get_extinction_factors(int medium)
{
	vec3 factors = vec3(0);
	if(medium == MEDIUM_WATER)
		factors = vec3(0.035, 0.013, 0.012);
	else if(medium == MEDIUM_SLIME)
		factors = vec3(0.200, 0.010, 0.050);
	else if(medium == MEDIUM_LAVA)
		factors = vec3(0.001, 0.100, 0.300);

	return factors * global_ubo.pt_water_density;
}

vec3 extinction(int medium, float distance)
{
	vec3 factors = get_extinction_factors(medium);

	return exp(-factors * distance);
}
