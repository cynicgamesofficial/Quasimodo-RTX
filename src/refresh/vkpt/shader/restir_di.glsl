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

/*
 * Evaluate the target function p-hat for a stored light at an arbitrary surface.
 *
 * Used during both initial candidate generation and temporal re-evaluation.
 * This is the SOLE definition of the target function; both passes must agree.
 *
 * Polygon lights:  spherical_tri_area * luminance(|emission|) * style
 * Dynamic lights:  sphere-irradiance  * luminance(color)      * NdotL
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

		vec3  c     = center - position;
		float dist  = length(c);
		float rdist = 1.0 / max(dist, 1e-6);
		vec3  L     = c * rdist;
		float NdotL = max(0.0, dot(normal, L));

		if(dot(L, geo_normal) <= 0.0)
			return 0.0;

		// Approximate irradiance from a sphere (same formula as compute_dynlight_sphere)
		float irradiance = 2.0 * (1.0 - sqrt(max(0.0, 1.0 - square(radius * rdist))));

		return luminance(color) * irradiance * NdotL;
	}
	else
	{
		uint poly_idx = restir_get_light_index(lightData);
		if(poly_idx >= MAX_LIGHT_POLYS)
			return 0.0;

		LightPolygon light = get_light_polygon(poly_idx);

		// Sky polygon lights encode a negative sentinel in light.color ---
		// not supported in Milestone 1.
		if(light.color.r < 0.0)
			return 0.0;

		// spherical_tri_area already includes a BRDF-weighted factor
		float area = spherical_tri_area(light.positions, position, normal, view_dir,
			phong_exp, phong_scale, phong_weight);
		float light_lum = luminance(abs(light.color)) * light.light_style_scale;

		return area * light_lum;
	}
}

#endif /* _RESTIR_DI_GLSL */
