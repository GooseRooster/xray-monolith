#pragma once

/*
 * SSE-optimized Ray-Box Intersection
 * Provides ~4x faster collision detection vs FPU fallback
 *
 * Ported from IXRAY engine
 * Adapted for X-Ray Monolith 
 *
 * Usage:
 *   ray_t ray;
 *   ray.pos.set(origin); ray.pos.pad = 0;
 *   ray.fwd_dir.set(direction); ray.fwd_dir.pad = 0;
 *   ray.inv_dir.set(1.f/direction.x, 1.f/direction.y, 1.f/direction.z); ray.inv_dir.pad = 0;
 *
 *   float dist;
 *   if (_box_sse(ray, center, radius, dist)) {
 *       // Hit at distance 'dist'
 *   }
 */

#include <xmmintrin.h>

// 16-byte alignment for SSE operations
#ifndef _MM_ALIGN16
#	define _MM_ALIGN16		__declspec(align(16))
#endif

// SSE-aligned vector with padding for 16-byte alignment
struct _MM_ALIGN16 vec_t : public Fvector3 {
	float pad;
};

// Axis-Aligned Bounding Box for SSE operations
struct _MM_ALIGN16 aabb_t {
	vec_t min;
	vec_t max;
};

// Ray structure for intersection tests
struct _MM_ALIGN16 ray_t {
	vec_t pos;      // Ray origin
	vec_t inv_dir;  // Inverse direction (1/dir) - precomputed for performance
	vec_t fwd_dir;  // Forward direction
};

// Ray segment for near/far intersection distances
struct ray_segment_t {
	float t_near, t_far;
};

// Type punning helper for float/int bit manipulation
ICF u32& uf(float& x) { return (u32&)x; }

//-----------------------------------------------------------------------------
// FPU Fallback - Used when SSE is not suitable or for debugging
//-----------------------------------------------------------------------------
ICF BOOL isect_fpu(const Fvector& min, const Fvector& max, const ray_t& ray, Fvector& coord)
{
	Fvector MaxT;
	MaxT.x = MaxT.y = MaxT.z = -1.0f;
	BOOL Inside = TRUE;

	// Find candidate planes
	if (ray.pos[0] < min[0]) {
		coord[0] = min[0];
		Inside = FALSE;
		if (uf(ray.inv_dir[0])) MaxT[0] = (min[0] - ray.pos[0]) * ray.inv_dir[0];
	}
	else if (ray.pos[0] > max[0]) {
		coord[0] = max[0];
		Inside = FALSE;
		if (uf(ray.inv_dir[0])) MaxT[0] = (max[0] - ray.pos[0]) * ray.inv_dir[0];
	}
	if (ray.pos[1] < min[1]) {
		coord[1] = min[1];
		Inside = FALSE;
		if (uf(ray.inv_dir[1])) MaxT[1] = (min[1] - ray.pos[1]) * ray.inv_dir[1];
	}
	else if (ray.pos[1] > max[1]) {
		coord[1] = max[1];
		Inside = FALSE;
		if (uf(ray.inv_dir[1])) MaxT[1] = (max[1] - ray.pos[1]) * ray.inv_dir[1];
	}
	if (ray.pos[2] < min[2]) {
		coord[2] = min[2];
		Inside = FALSE;
		if (uf(ray.inv_dir[2])) MaxT[2] = (min[2] - ray.pos[2]) * ray.inv_dir[2];
	}
	else if (ray.pos[2] > max[2]) {
		coord[2] = max[2];
		Inside = FALSE;
		if (uf(ray.inv_dir[2])) MaxT[2] = (max[2] - ray.pos[2]) * ray.inv_dir[2];
	}

	// Ray origin inside bounding box
	if (Inside) {
		coord = ray.pos;
		return true;
	}

	// Get largest MaxT for final choice of intersection
	u32 WhichPlane = 0;
	if (MaxT[1] > MaxT[0]) WhichPlane = 1;
	if (MaxT[2] > MaxT[WhichPlane]) WhichPlane = 2;

	// Check final candidate actually inside box (if max < 0)
	if (uf(MaxT[WhichPlane]) & 0x80000000) return false;

	if (0 == WhichPlane) {
		coord[1] = ray.pos[1] + MaxT[0] * ray.fwd_dir[1];
		if ((coord[1] < min[1]) || (coord[1] > max[1])) return false;
		coord[2] = ray.pos[2] + MaxT[0] * ray.fwd_dir[2];
		if ((coord[2] < min[2]) || (coord[2] > max[2])) return false;
		return true;
	}
	if (1 == WhichPlane) {
		coord[0] = ray.pos[0] + MaxT[1] * ray.fwd_dir[0];
		if ((coord[0] < min[0]) || (coord[0] > max[0])) return false;
		coord[2] = ray.pos[2] + MaxT[1] * ray.fwd_dir[2];
		if ((coord[2] < min[2]) || (coord[2] > max[2])) return false;
		return true;
	}
	if (2 == WhichPlane) {
		coord[0] = ray.pos[0] + MaxT[2] * ray.fwd_dir[0];
		if ((coord[0] < min[0]) || (coord[0] > max[0])) return false;
		coord[1] = ray.pos[1] + MaxT[2] * ray.fwd_dir[1];
		if ((coord[1] < min[1]) || (coord[1] > max[1])) return false;
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// SSE Intrinsic Macros - readable wrappers for verbose intrinsics
//-----------------------------------------------------------------------------
#define loadps(mem)         _mm_load_ps((const float * const)(mem))
#define storess(ss,mem)     _mm_store_ss((float * const)(mem),(ss))
#define minss               _mm_min_ss
#define maxss               _mm_max_ss
#define minps               _mm_min_ps
#define maxps               _mm_max_ps
#define mulps               _mm_mul_ps
#define subps               _mm_sub_ps
#define rotatelps(ps)       _mm_shuffle_ps((ps),(ps), 0x39)  // a,b,c,d -> b,c,d,a
#define muxhps(low,high)    _mm_movehl_ps((low),(high))      // low{a,b,c,d}|high{e,f,g,h} = {c,d,g,h}

// Infinity constants for NaN filtering
static const float flt_plus_inf = -logf(0);
static const float _MM_ALIGN16
ps_cst_plus_inf[4]  = { flt_plus_inf,  flt_plus_inf,  flt_plus_inf,  flt_plus_inf },
ps_cst_minus_inf[4] = { -flt_plus_inf, -flt_plus_inf, -flt_plus_inf, -flt_plus_inf };

//-----------------------------------------------------------------------------
// SSE Ray-Box Intersection
// Returns TRUE if ray intersects box, with distance in 'dist'
//-----------------------------------------------------------------------------
ICF BOOL isect_sse(const aabb_t& box, const ray_t& ray, float& dist)
{
	// Load infinity constants for NaN filtering
	const __m128
		plus_inf = loadps(ps_cst_plus_inf),
		minus_inf = loadps(ps_cst_minus_inf);

	// Load box and ray data (requires 16-byte alignment)
	const __m128
		box_min = loadps(&box.min),
		box_max = loadps(&box.max),
		pos = loadps(&ray.pos),
		inv_dir = loadps(&ray.inv_dir);

	// Compute intersection distances for each axis
	const __m128 l1 = mulps(subps(box_min, pos), inv_dir);
	const __m128 l2 = mulps(subps(box_max, pos), inv_dir);

	// Filter NaNs that occur when inv_dir is +/- inf and (box - pos) is 0
	// inf * 0 = NaN, which we need to handle gracefully
	const __m128 filtered_l1a = minps(l1, plus_inf);
	const __m128 filtered_l2a = minps(l2, plus_inf);

	const __m128 filtered_l1b = maxps(l1, minus_inf);
	const __m128 filtered_l2b = maxps(l2, minus_inf);

	// Compute slab intersections
	__m128 lmax = maxps(filtered_l1a, filtered_l2a);
	__m128 lmin = minps(filtered_l1b, filtered_l2b);

	// Reduce to single min/max across all axes
	const __m128 lmax0 = rotatelps(lmax);
	const __m128 lmin0 = rotatelps(lmin);
	lmax = minss(lmax, lmax0);
	lmin = maxss(lmin, lmin0);

	const __m128 lmax1 = muxhps(lmax, lmax);
	const __m128 lmin1 = muxhps(lmin, lmin);
	lmax = minss(lmax, lmax1);
	lmin = maxss(lmin, lmin1);

	// Check if intersection is valid: lmax >= 0 && lmax >= lmin
	const BOOL ret = _mm_comige_ss(lmax, _mm_setzero_ps()) & _mm_comige_ss(lmax, lmin);

	storess(lmin, &dist);
	return ret;
}

//-----------------------------------------------------------------------------
// FPU Helper Functions
//-----------------------------------------------------------------------------
ICF BOOL _box_fpu(ray_t& ray, const Fvector& n_C, const float n_R, Fvector& coord)
{
	float n_vR = 2 * n_R;
	Fbox BB;
	BB.set(n_C.x - n_vR, n_C.y - n_vR, n_C.z - n_vR, n_C.x + n_vR, n_C.y + n_vR, n_C.z + n_vR);
	return isect_fpu(BB.min, BB.max, ray, coord);
}

ICF BOOL _box_fpu(ray_t& ray, const Fvector& bCenter, const Fvector& bExtents, Fvector& coord)
{
	Fbox BB;
	BB.min.sub(bCenter, bExtents);
	BB.max.add(bCenter, bExtents);
	return isect_fpu(BB.min, BB.max, ray, coord);
}

//-----------------------------------------------------------------------------
// SSE Helper Functions
//-----------------------------------------------------------------------------
ICF BOOL _box_sse(ray_t& ray, const Fvector& n_C, const float n_R, float& dist)
{
	aabb_t box;

	// Build AABB from center + radius using SSE
	__m128 NR = _mm_load_ss((float*)&n_R);
	__m128 NC = _mm_unpacklo_ps(_mm_load_ss((float*)&n_C.x), _mm_load_ss((float*)&n_C.y));
	NR = _mm_add_ss(NR, NR);
	NC = _mm_movelh_ps(NC, _mm_load_ss((float*)&n_C.z));
	NR = _mm_shuffle_ps(NR, NR, _MM_SHUFFLE(1, 0, 0, 0));

	_mm_store_ps((float*)&box.min, _mm_sub_ps(NC, NR));
	_mm_store_ps((float*)&box.max, _mm_add_ps(NC, NR));

	return isect_sse(box, ray, dist);
}

ICF BOOL _box_sse(ray_t& ray, const Fvector& bCenter, const Fvector& bExtents, float& dist)
{
	aabb_t box;

	// Build AABB from center + extents using SSE
	__m128 CN = _mm_unpacklo_ps(_mm_load_ss((float*)&bCenter.x), _mm_load_ss((float*)&bCenter.y));
	CN = _mm_movelh_ps(CN, _mm_load_ss((float*)&bCenter.z));
	__m128 EX = _mm_unpacklo_ps(_mm_load_ss((float*)&bExtents.x), _mm_load_ss((float*)&bExtents.y));
	EX = _mm_movelh_ps(EX, _mm_load_ss((float*)&bExtents.z));

	_mm_store_ps((float*)&box.min, _mm_sub_ps(CN, EX));
	_mm_store_ps((float*)&box.max, _mm_add_ps(CN, EX));

	return isect_sse(box, ray, dist);
}
