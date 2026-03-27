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
 *        bits [31:14] = reserved (zero)
 *
 *        Polygon lights:  index is the polygon light index into LightBuffer.light_polys[].
 *        Dynamic lights:  index is the dynamic light index into global_ubo.dyn_light_data[].
 *        Encoding: lightData = isDynamic ? ((1u << 13) | dynIdx) : polyIdx
 *
 *   .y = floatBitsToUint(targetPdf)
 *        The target function p-hat evaluated at the selected sample.
 *        For Milestone 1 this is: luminance(unshadowed_radiance * BRDF * NdotL).
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
 *   - The target PDF is always re-evaluated at the shading point since it depends on the
 *     surface BRDF, normal, and distance to the light sample point.
 *   - During temporal reuse, the previous reservoir's target PDF is re-evaluated at the
 *     CURRENT pixel's surface (not the previous pixel's surface).
 */

#define RESTIR_M_CAP 20
#define RESTIR_LIGHT_FLAG_DYNAMIC (1u << 13)

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
reservoir_insert(inout Reservoir r, inout float weightSum, uint lightData, float targetPdf, float risWeight, float rng)
{
	weightSum += risWeight;
	r.M += 1;

	if(rng * weightSum < risWeight)
	{
		r.lightData = lightData;
		r.targetPdf = targetPdf;
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
reservoir_combine(inout Reservoir a, inout float weightSumA, Reservoir b, float targetPdf_b_at_a, float rng)
{
	float b_weight = targetPdf_b_at_a * b.W * float(b.M);

	weightSumA += b_weight;
	a.M += b.M;

	if(rng * weightSumA < b_weight)
	{
		a.lightData = b.lightData;
		a.targetPdf = targetPdf_b_at_a;
	}
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
restir_is_dynamic_light(uint lightData)
{
	return (lightData & RESTIR_LIGHT_FLAG_DYNAMIC) != 0;
}

uint
restir_get_light_index(uint lightData)
{
	return lightData & 0x1FFFu; // lower 13 bits
}

#endif /* _RESTIR_DI_GLSL */
