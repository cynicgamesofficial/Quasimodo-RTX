/*
Copyright (C) 2026 Quasimodo-RTX contributors.

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

#ifndef _RESTIR_DI_GLSL
#define _RESTIR_DI_GLSL

/*
 * ReSTIR DI reservoir structure and utilities.
 *
 * Reservoir packing into uvec4 (rgba32ui image):
 *
 *   .x = lightData
 *        bits [12:0]  = light index (0..4095 polygon, 4096..4127 dynamic)
 *        bit  [13]    = 1 if dynamic light, 0 if polygon light
 *        bit  [14]    = 1 if sun (no index; bits [12:0] and [13] ignored)
 *        bits [31:15] = reserved (zero)
 *
 *        Polygon lights:  index is the polygon light index into LightBuffer.light_polys[].
 *        Dynamic lights:  index is the dynamic light index into global_ubo.dyn_light_data[].
 *        Sun:             no index needed — there is exactly one sun.
 *        Encoding: lightData = isDynamic ? ((1u << 13) | dynIdx) : isSun ? (1u << 14) : polyIdx
 *
 *   .y = floatBitsToUint(targetPdf)
 *        The pure target function p-hat evaluated at the selected sample.
 *        Polygon lights:  spherical_tri_area * luminance(|emission|) * style.
 *        Dynamic lights:  luminance(color) * sphere_irradiance * NdotL.
 *        Evaluated by evaluate_restir_target(); must never contain 1/source_pdf.
 *        Stored as float to avoid precision loss at low values.
 *
 *   .z = M (sample count)
 *        Number of candidates this reservoir has seen.
 *        Capped at RESTIR_M_CAP during temporal reuse to limit variance from stale samples.
 *
 *   .w = floatBitsToUint(W)
 *        The final unbiased contribution weight: W = (1/targetPdf) * (weightSum / M).
 *        Shading multiplies: radiance = eval_light(selected) * W.
 *        Stored after reservoir_finalize().
 *
 * Target/source PDF reconstruction:
 *   - The source PDF for a polygon light = the light selection PDF from sample_polygonal_lights()
 *     i.e. (cluster CDF weight) / (sum of cluster CDF weights) * geometric solid-angle PDF.
 *   - The source PDF for a dynamic light = 1 / num_dyn_lights (uniform random selection).
 *   - The source PDF for the sun = 1 (exactly one sun; always attempted).
 *   - The target PDF is always re-evaluated at the shading point since it depends on the
 *     surface BRDF, normal, and distance to the light sample point.
 *   - During temporal reuse, the previous reservoir's target PDF is re-evaluated at the
 *     CURRENT pixel's surface (not the previous pixel's surface).
 */

#define RESTIR_M_CAP 20
#define RESTIR_LIGHT_FLAG_DYNAMIC (1u << 13)
#define RESTIR_LIGHT_FLAG_SUN     (1u << 14)

struct Reservoir
{
	uint  lightData;
	float targetPdf;
	uint  M;
	float W;
};

Reservoir
reservoir_empty()
{
	Reservoir r;
	r.lightData = 0;
	r.targetPdf = 0.0;
	r.M = 0;
	r.W = 0.0;
	return r;
}

uvec4
reservoir_pack(Reservoir r)
{
	return uvec4(
		r.lightData,
		floatBitsToUint(r.targetPdf),
		r.M,
		floatBitsToUint(r.W)
	);
}

Reservoir
reservoir_unpack(uvec4 packed)
{
	Reservoir r;
	r.lightData = packed.x;
	r.targetPdf = uintBitsToFloat(packed.y);
	r.M         = packed.z;
	r.W         = uintBitsToFloat(packed.w);
	return r;
}

/*
 * RIS streaming insertion.
 *
 * Inserts a candidate into the reservoir using the standard RIS streaming algorithm:
 *   weightSum += risWeight
 *   with probability (risWeight / weightSum), replace the stored sample.
 *
 * risWeight = targetPdf / sourcePdf for this candidate.
 * rng must be uniform in [0, 1).
 *
 * Returns true if the candidate replaced the stored sample.
 */
bool
reservoir_insert(inout Reservoir r, inout float weightSum, uint lightData, float targetPdf, float risWeight, float rng,
	vec3 candidate_sample_pos, inout vec3 stored_sample_pos)
{
	weightSum += risWeight;
	r.M += 1;

	if(rng * weightSum < risWeight)
	{
		r.lightData = lightData;
		r.targetPdf = targetPdf;
		stored_sample_pos = candidate_sample_pos;
		return true;
	}
	return false;
}

/*
 * Combine two reservoirs (temporal or spatial reuse).
 *
 * 'b' is the neighbor/previous reservoir.
 * 'targetPdf_b_at_a' is the target function of b's selected sample evaluated at the
 * surface of reservoir 'a' (the current pixel).
 *
 * The combined reservoir is written into 'a' (in-place).
 * 'weightSumA' must be initialized to (a.targetPdf * a.W * a.M) before calling.
 * After combining, call reservoir_finalize().
 *
 * rng must be uniform in [0, 1).
 */
void
reservoir_combine(inout Reservoir a, inout float weightSumA, Reservoir b, float targetPdf_b_at_a, float rng,
	vec3 prev_sample_pos, inout vec3 stored_sample_pos)
{
	float b_weight = targetPdf_b_at_a * b.W * float(b.M);

	weightSumA += b_weight;
	a.M += b.M;

	if(rng * weightSumA < b_weight)
	{
		a.lightData = b.lightData;
		a.targetPdf = targetPdf_b_at_a;
		stored_sample_pos = prev_sample_pos;
	}
	/* else: keep stored_sample_pos (already equals current-frame sample before combine). */
}

/*
 * Finalize a reservoir after all insertions or combinations.
 * Computes the unbiased contribution weight W.
 */
void
reservoir_finalize(inout Reservoir r, float weightSum)
{
	if(r.targetPdf > 0.0 && r.M > 0)
		r.W = weightSum / (float(r.M) * r.targetPdf);
	else
		r.W = 0.0;
}

/*
 * Surface similarity test for temporal reuse.
 * Rejects the previous reservoir if the surfaces differ too much.
 *
 * Thresholds:
 *   normal: dot >= 0.906 (~25 degrees)
 *   depth:  ratio <= 10%
 */
bool
restir_surface_similar(vec3 normal_a, vec3 normal_b, float depth_a, float depth_b)
{
	if(dot(normal_a, normal_b) < 0.906)
		return false;

	float depth_ratio = max(depth_a, depth_b) / max(min(depth_a, depth_b), 1e-6);
	if(depth_ratio > 1.1)
		return false;

	return true;
}

/* Helpers to encode/decode the lightData field */
uint
restir_encode_polygon_light(uint poly_index)
{
	return poly_index;
}

uint
restir_encode_dynamic_light(uint dyn_index)
{
	return RESTIR_LIGHT_FLAG_DYNAMIC | dyn_index;
}

bool
restir_is_sun_light(uint lightData)
{
	return (lightData & RESTIR_LIGHT_FLAG_SUN) != 0;
}

bool
restir_is_dynamic_light(uint lightData)
{
	return !restir_is_sun_light(lightData) && (lightData & RESTIR_LIGHT_FLAG_DYNAMIC) != 0;
}

uint
restir_encode_sun()
{
	return RESTIR_LIGHT_FLAG_SUN;
}

uint
restir_get_light_index(uint lightData)
{
	return lightData & 0x1FFFu; // lower 13 bits
}

bool
restir_is_sky_polygon_light(LightPolygon light)
{
	return light.color.r < 0.0;
}

float
restir_get_polygon_target_weight(LightPolygon light)
{
	float light_lum = luminance(light.color) * light.light_style_scale;

	if(light_lum < 0.0 && global_ubo.environment_type == ENVIRONMENT_DYNAMIC)
	{
		return clamp(sun_color_ubo.sky_luminance,
			global_ubo.pt_min_log_sky_luminance,
			global_ubo.pt_max_log_sky_luminance);
	}

	return abs(light_lum);
}

bool
restir_is_finite_float(float v)
{
	return !isnan(v) && !isinf(v);
}

bool
restir_is_finite_vec3(vec3 v)
{
	return !any(isnan(v)) && !any(isinf(v));
}

bool
restir_validate_dynamic_sample_identity(uint dyn_idx, vec3 sample_pos)
{
	if(dyn_idx >= global_ubo.num_dyn_lights)
		return false;

	if(!restir_is_finite_vec3(sample_pos))
		return false;

	vec3 center = global_ubo.dyn_light_data[dyn_idx].center;
	vec3 color = global_ubo.dyn_light_data[dyn_idx].color;
	float radius = global_ubo.dyn_light_data[dyn_idx].radius;
	uint light_type = global_ubo.dyn_light_data[dyn_idx].type & 0xffffu;
	uint light_style = global_ubo.dyn_light_data[dyn_idx].type >> 16;

	if(!restir_is_finite_vec3(center) || !restir_is_finite_vec3(color))
		return false;
	if(!restir_is_finite_float(radius) || radius <= 0.0)
		return false;

	if(light_type == DYNLIGHT_SPHERE)
	{
		float dist = length(sample_pos - center);
		if(!restir_is_finite_float(dist))
			return false;

		float sphere_tol = max(1e-3, 0.05 * radius);
		return abs(dist - radius) <= sphere_tol;
	}
	else if(light_type == DYNLIGHT_SPOT)
	{
		if(light_style != DYNLIGHT_SPOT_EMISSION_PROFILE_FALLOFF &&
		   light_style != DYNLIGHT_SPOT_EMISSION_PROFILE_AXIS_ANGLE_TEXTURE)
			return false;

		vec3 axis = global_ubo.dyn_light_data[dyn_idx].spot_direction;
		if(!restir_is_finite_vec3(axis))
			return false;

		float axis_len = length(axis);
		if(!restir_is_finite_float(axis_len) || axis_len <= 1e-4)
			return false;

		vec3 rel = sample_pos - center;
		if(!restir_is_finite_vec3(rel))
			return false;

		// Validate against the same emitter-disk basis used by compute_dynlight_spot().
		// The client path uploads spotlight directions without normalizing them first,
		// so validating against an idealized normalized axis can reject samples that
		// the legacy path still considers valid.
		mat3 onb = construct_ONB_frisvad(axis);
		vec3 basis_u = onb[0];
		vec3 basis_v = onb[2];

		float g00 = dot(basis_u, basis_u);
		float g01 = dot(basis_u, basis_v);
		float g11 = dot(basis_v, basis_v);
		float det = g00 * g11 - g01 * g01;
		if(!restir_is_finite_float(det) || abs(det) <= 1e-12)
			return false;

		float d0 = dot(rel, basis_u);
		float d1 = dot(rel, basis_v);
		float coeff_u = (d0 * g11 - d1 * g01) / det;
		float coeff_v = (d1 * g00 - d0 * g01) / det;
		if(!restir_is_finite_float(coeff_u) || !restir_is_finite_float(coeff_v))
			return false;

		vec3 reconstructed = basis_u * coeff_u + basis_v * coeff_v;
		float residual = length(rel - reconstructed);
		float plane_tol = max(1e-3, 0.01 * radius * max(length(basis_u), length(basis_v)));
		if(!restir_is_finite_float(residual) || residual > plane_tol)
			return false;

		float disk_u = coeff_u / max(radius, 1e-6);
		float disk_v = coeff_v / max(radius, 1e-6);
		if(disk_u * disk_u + disk_v * disk_v > square(1.05))
			return false;

		return true;
	}

	return false;
}

bool
restir_validate_polygon_sample_identity(uint poly_idx, vec3 sample_pos)
{
	if(poly_idx >= MAX_LIGHT_POLYS)
		return false;
	if(!restir_is_finite_vec3(sample_pos))
		return false;

	LightPolygon light = get_light_polygon(poly_idx);
	if(restir_is_sky_polygon_light(light))
		return false;
	if(!restir_is_finite_vec3(light.color))
		return false;

	vec3 a = light.positions[0];
	vec3 b = light.positions[1];
	vec3 c = light.positions[2];
	if(!restir_is_finite_vec3(a) || !restir_is_finite_vec3(b) || !restir_is_finite_vec3(c))
		return false;

	vec3 e0 = b - a;
	vec3 e1 = c - a;
	vec3 n = cross(e0, e1);
	float n_len = length(n);
	if(!restir_is_finite_float(n_len) || n_len <= 1e-8)
		return false;

	float edge_ab = length(e0);
	float edge_bc = length(c - b);
	float edge_ca = length(e1);
	if(!restir_is_finite_float(edge_ab) || !restir_is_finite_float(edge_bc) || !restir_is_finite_float(edge_ca))
		return false;
	float maxEdgeLen = max(edge_ab, max(edge_bc, edge_ca));

	vec3 n_hat = n / n_len;
	float plane_dist = abs(dot(n_hat, sample_pos - a));
	if(plane_dist > max(1e-3, 5e-3 * maxEdgeLen))
		return false;

	float d00 = dot(e0, e0);
	float d01 = dot(e0, e1);
	float d11 = dot(e1, e1);
	vec3 v2 = sample_pos - a;
	float d20 = dot(v2, e0);
	float d21 = dot(v2, e1);
	float denom = d00 * d11 - d01 * d01;
	if(!restir_is_finite_float(denom) || abs(denom) <= 1e-12)
		return false;

	float v = (d11 * d20 - d01 * d21) / denom;
	float w = (d00 * d21 - d01 * d20) / denom;
	float u = 1.0 - v - w;
	if(!restir_is_finite_float(u) || !restir_is_finite_float(v) || !restir_is_finite_float(w))
		return false;

	const float bary_min = -5e-3;
	const float bary_max = 1.005;
	return (u >= bary_min && v >= bary_min && w >= bary_min &&
		u <= bary_max && v <= bary_max && w <= bary_max);
}

bool
restir_validate_light_sample_identity(uint lightData, vec3 sample_pos)
{
	if(restir_is_sun_light(lightData))
		return restir_is_finite_vec3(sample_pos);

	if(restir_is_dynamic_light(lightData))
	{
		uint dyn_idx = restir_get_light_index(lightData);
		return restir_validate_dynamic_sample_identity(dyn_idx, sample_pos);
	}

	uint poly_idx = restir_get_light_index(lightData);
	return restir_validate_polygon_sample_identity(poly_idx, sample_pos);
}

float
restir_dynlight_spot_falloff(uint dyn_idx, uint light_style, vec3 Ldir)
{
	mat3 onb = construct_ONB_frisvad(global_ubo.dyn_light_data[dyn_idx].spot_direction);
	vec3 L_l = -Ldir * onb;
	float cosTheta = L_l.y;
	float falloff;

	if(light_style == DYNLIGHT_SPOT_EMISSION_PROFILE_FALLOFF)
	{
		const vec2 spot_falloff = unpackHalf2x16(global_ubo.dyn_light_data[dyn_idx].spot_data);
		const float cosTotalWidth = spot_falloff.x;
		const float cosFalloffStart = spot_falloff.y;

		if(cosTheta < cosTotalWidth)
			falloff = 0.0;
		else if(cosTheta > cosFalloffStart)
			falloff = 1.0;
		else
		{
			float delta = (cosTheta - cosTotalWidth) / (cosFalloffStart - cosTotalWidth);
			falloff = (delta * delta) * (delta * delta);
		}
	}
	else if(light_style == DYNLIGHT_SPOT_EMISSION_PROFILE_AXIS_ANGLE_TEXTURE)
	{
		const uint spot_data = global_ubo.dyn_light_data[dyn_idx].spot_data;
		const float theta = acos(cosTheta);
		const float totalWidth = unpackHalf2x16(spot_data).x;
		const uint texture_num = spot_data >> 16;

		if(cosTheta >= 0.0)
		{
			float tc = clamp(theta / totalWidth, 0.0, 1.0);
			falloff = global_texture(texture_num, vec2(tc, 0.0)).r;
		}
		else
			falloff = 0.0;
	}
	else
		falloff = 0.0;

	return falloff;
}

float
restir_dynlight_spot_irradiance(uint dyn_idx, uint light_style, vec3 position, vec3 sample_pos)
{
	vec3 c = sample_pos - position;
	float dist = length(c);
	float rdist = 1.0 / max(dist, 1e-6);
	vec3 Ldir = c * rdist;
	float falloff = restir_dynlight_spot_falloff(dyn_idx, light_style, Ldir);
	float irradiance = 2.0 * falloff * square(rdist);
	return min(irradiance, 2.0 * M_PI);
}

float
restir_dynamic_brdf_weight(vec3 normal, vec3 view_dir, vec3 L,
	float phong_exp, float phong_scale, float phong_weight)
{
	float specular = phong(normal, L, view_dir, phong_exp) * phong_scale;
	return mix(1.0, specular, phong_weight);
}

float
evaluate_restir_target_sampled(uint lightData, vec3 sample_pos,
	vec3 position, vec3 normal, vec3 geo_normal, vec3 view_dir,
	float phong_exp, float phong_scale, float phong_weight)
{
	if(restir_is_sun_light(lightData))
	{
		if(global_ubo.sun_visible == 0)
			return 0.0;

		float NdotL  = max(0.0, dot(normal, global_ubo.sun_direction));
		float GNdotL = dot(geo_normal, global_ubo.sun_direction);

		if(NdotL <= 0.0 || GNdotL <= 0.0)
			return 0.0;

		return luminance(sun_color_ubo.sun_color) * NdotL;
	}

	if(restir_is_dynamic_light(lightData))
	{
		uint dyn_idx = restir_get_light_index(lightData);
		if(dyn_idx >= global_ubo.num_dyn_lights)
			return 0.0;

		vec3 color = global_ubo.dyn_light_data[dyn_idx].color;
		uint light_type = global_ubo.dyn_light_data[dyn_idx].type & 0xffff;
		uint light_style = global_ubo.dyn_light_data[dyn_idx].type >> 16;

		if(light_type == DYNLIGHT_SPHERE)
		{
			vec3 center = global_ubo.dyn_light_data[dyn_idx].center;
			float radius = global_ubo.dyn_light_data[dyn_idx].radius;

			vec3 c = center - position;
			float dist = length(c);
			float rdist = 1.0 / max(dist, 1e-6);
			vec3 L = c * rdist;

			if(dot(L, geo_normal) <= 0.0)
				return 0.0;

			float irradiance = 2.0 * (1.0 - sqrt(max(0.0, 1.0 - square(radius * rdist))));
			float brdf_weight = restir_dynamic_brdf_weight(
				normal, view_dir, L, phong_exp, phong_scale, phong_weight);
			return luminance(color) * irradiance * brdf_weight;
		}
		else if(light_type == DYNLIGHT_SPOT)
		{
			vec3 c = sample_pos - position;
			if(dot(c, geo_normal) <= 0.0)
				return 0.0;

			float rdist = 1.0 / max(length(c), 1e-6);
			vec3 L = c * rdist;
			float irradiance = restir_dynlight_spot_irradiance(dyn_idx, light_style, position, sample_pos);
			float brdf_weight = restir_dynamic_brdf_weight(
				normal, view_dir, L, phong_exp, phong_scale, phong_weight);
			return luminance(color) * irradiance * brdf_weight;
		}
		else
		{
			return 0.0;
		}
	}
	else
	{
		uint poly_idx = restir_get_light_index(lightData);
		if(poly_idx >= MAX_LIGHT_POLYS)
			return 0.0;

		LightPolygon light = get_light_polygon(poly_idx);
		if(restir_is_sky_polygon_light(light))
			return 0.0;

		// Reject the stored sample point if it is geometrically behind this
		// surface.  Without this the spatial (and temporal) re-evaluation can
		// return a non-zero targetPdf for a neighbour's light that sits below
		// the current pixel's geometric plane, causing the shadow ray in
		// direct_lighting.rgen to cross through geometry and produce the
		// flickering shadow artefacts visible during camera motion.
		if(dot(sample_pos - position, geo_normal) <= 0.0)
			return 0.0;

		float area = spherical_tri_area(light.positions, position, normal, view_dir,
			phong_exp, phong_scale, phong_weight);
		float light_lum = restir_get_polygon_target_weight(light);

		return area * light_lum;
	}
}

/*
 * Evaluate the target function p-hat for a stored light at an arbitrary surface.
 *
 * Used during both initial candidate generation and temporal re-evaluation.
 * This is the SOLE definition of the target function; both passes must agree.
 *
 * Polygon lights:  spherical_tri_area * polygon-target-weight
 *                  where sky polygons use the same negative-color rule and
 *                  luminance clamping as sample_polygonal_lights().
 * Dynamic lights:  dynamic-irradiance * luminance(color)      * specular-aware BRDF weight
 * Sun:             luminance(sun_color) * NdotL
 *
 * Returns 0 if the light is invisible from the surface.
 */
float
evaluate_restir_target(uint lightData,
	vec3 position, vec3 normal, vec3 geo_normal, vec3 view_dir,
	float phong_exp, float phong_scale, float phong_weight)
{
	if(restir_is_sun_light(lightData))
	{
		if(global_ubo.sun_visible == 0)
			return 0.0;

		float NdotL  = max(0.0, dot(normal, global_ubo.sun_direction));
		float GNdotL = dot(geo_normal, global_ubo.sun_direction);

		if(NdotL <= 0.0 || GNdotL <= 0.0)
			return 0.0;

		return luminance(sun_color_ubo.sun_color) * NdotL;
	}

	if(restir_is_dynamic_light(lightData))
	{
		uint dyn_idx = restir_get_light_index(lightData);
		if(dyn_idx >= global_ubo.num_dyn_lights)
			return 0.0;

		vec3  center = global_ubo.dyn_light_data[dyn_idx].center;
		vec3  color  = global_ubo.dyn_light_data[dyn_idx].color;
		float radius = global_ubo.dyn_light_data[dyn_idx].radius;
		uint  light_type = global_ubo.dyn_light_data[dyn_idx].type & 0xffffu;
		uint  light_style = global_ubo.dyn_light_data[dyn_idx].type >> 16;

		if(light_type == DYNLIGHT_SPHERE)
		{
			vec3  c     = center - position;
			float dist  = length(c);
			float rdist = 1.0 / max(dist, 1e-6);
			vec3  L     = c * rdist;

			if(dot(L, geo_normal) <= 0.0)
				return 0.0;

			// Approximate irradiance from a sphere (same formula as compute_dynlight_sphere)
			float irradiance = 2.0 * (1.0 - sqrt(max(0.0, 1.0 - square(radius * rdist))));
			float brdf_weight = restir_dynamic_brdf_weight(
				normal, view_dir, L, phong_exp, phong_scale, phong_weight);
			return luminance(color) * irradiance * brdf_weight;
		}
		else if(light_type == DYNLIGHT_SPOT)
		{
			vec3 c = center - position;
			if(dot(c, geo_normal) <= 0.0)
				return 0.0;

			float rdist = 1.0 / max(length(c), 1e-6);
			vec3 L = c * rdist;
			float irradiance = restir_dynlight_spot_irradiance(dyn_idx, light_style, position, center);
			float brdf_weight = restir_dynamic_brdf_weight(
				normal, view_dir, L, phong_exp, phong_scale, phong_weight);
			return luminance(color) * irradiance * brdf_weight;
		}
		else
		{
			return 0.0;
		}
	}
	else
	{
		uint poly_idx = restir_get_light_index(lightData);
		if(poly_idx >= MAX_LIGHT_POLYS)
			return 0.0;

		LightPolygon light = get_light_polygon(poly_idx);
		if(restir_is_sky_polygon_light(light))
			return 0.0;

		// spherical_tri_area already includes a BRDF-weighted factor
		float area = spherical_tri_area(light.positions, position, normal, view_dir,
			phong_exp, phong_scale, phong_weight);
		float light_lum = restir_get_polygon_target_weight(light);

		return area * light_lum;
	}
}

#endif /* _RESTIR_DI_GLSL */
