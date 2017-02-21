#include <algorithm>
#include <iostream>
#include <fstream>
#include <numeric>
#include <limits>
#include <cmath>
#include <map>

#include "Tools/Perf/MIPP/mipp.h"
#include "Tools/Perf/Transpose/transpose_selector.h"
#include "Tools/Display/bash_tools.h"
#include "Tools/Math/utils.h"

#include "Tools/Code/Polar/Patterns/Pattern_polar_r0.hpp"
#include "Tools/Code/Polar/Patterns/Pattern_polar_r0_left.hpp"
#include "Tools/Code/Polar/Patterns/Pattern_polar_r1.hpp"
#include "Tools/Code/Polar/Patterns/Pattern_polar_rep.hpp"
#include "Tools/Code/Polar/Patterns/Pattern_polar_rep_left.hpp"
#include "Tools/Code/Polar/Patterns/Pattern_polar_spc.hpp"
#include "Tools/Code/Polar/Patterns/Pattern_polar_std.hpp"

#include "Tools/Code/Polar/Pattern_polar_parser.hpp"

#include "Decoder_polar_SCL_fast_sys.hpp"

template <typename R> inline R           sat_m(const R           m) { return                       m           ; }
template <          > inline signed char sat_m(const signed char m) { return saturate<signed char>(m, -128, 63); }

template <typename R>
inline void normalize_scl_metrics(std::vector<R> &metrics, const int L)
{
}

template <>
inline void normalize_scl_metrics(std::vector<short> &metrics, const int L)
{
	auto min = *std::min_element(metrics.begin(), metrics.begin() + L);

	const auto norm = std::numeric_limits<short>::min() - min;

	for (auto i = 0; i < L; i++)
		metrics[i] += norm;
}

template <>
inline void normalize_scl_metrics(std::vector<signed char> &metrics, const int L)
{
	auto min = *std::min_element(metrics.begin(), metrics.begin() + L);

	const auto norm = std::numeric_limits<signed char>::min() - min;

	for (auto i = 0; i < L; i++)
		metrics[i] += norm;
}

template <typename B, typename R, class API_polar>
Decoder_polar_SCL_fast_sys<B,R,API_polar>
::Decoder_polar_SCL_fast_sys(const int& K, const int& N, const int& L, const mipp::vector<B>& frozen_bits,
                             const int n_frames, const std::string name)
: Decoder<B,R>  (K, N, n_frames, API_polar::get_n_frames(), name),
  m             (std::log2(N)),
  L             (L),
  frozen_bits   (frozen_bits),
  polar_patterns(N,
                 frozen_bits,
                 {new Pattern_polar_std,
                  new Pattern_polar_r0,
                  new Pattern_polar_r1,
                  new Pattern_polar_r0_left,
                  new Pattern_polar_rep_left,
                  new Pattern_polar_rep/*(2,3)*/,  // perf. degradation with REP nodes with fixed-point arithmetic
                  new Pattern_polar_spc(2,2)}, // perf. degradation with SPC nodes length > 4
                 1,
                 2),
  paths         (L),
  metrics       (L),
  Y_N           (                   N + mipp::nElReg<R>() ),
  l             (L, mipp::vector<R>(N + mipp::nElReg<R>())),
  s             (L, mipp::vector<B>(N + mipp::nElReg<B>())),
  metrics_vec   (3, std::vector<R>()),
  dup_count     (L, 0),
  bit_flips     (4 * L),
  is_even       (L),
  best_path     (0),
  n_active_paths(1),
  n_array_ref   (L, std::vector<int>(m)),
  path_2_array  (L, std::vector<int>(m)),
  sorter        (N),
//sorter_simd   (N),
  best_idx      (L),
  l_tmp         (N)
{
	static_assert(API_polar::get_n_frames() == 1, "The inter-frame API_polar is not supported.");
	static_assert(sizeof(B) == sizeof(R), "Sizes of the bits and reals have to be identical.");

	assert(is_power_of_2(L));

	metrics_vec[0].resize(L * 2);
	metrics_vec[1].resize(L * 4);
	metrics_vec[2].resize((L <= 2 ? 4 : 8) * L);
}

template <typename B, typename R, class API_polar>
Decoder_polar_SCL_fast_sys<B,R,API_polar>
::Decoder_polar_SCL_fast_sys(const int& K, const int& N, const int& L, const mipp::vector<B>& frozen_bits,
                             const std::vector<Pattern_polar_i*> polar_patterns, const int idx_r0, const int idx_r1,
                             const int n_frames, const std::string name)
: Decoder<B,R>  (K, N, n_frames, API_polar::get_n_frames(), name),
  m             (std::log2(N)),
  L             (L),
  frozen_bits   (frozen_bits),
  polar_patterns(N, frozen_bits, polar_patterns, idx_r0, idx_r1),
  paths         (L),
  metrics       (L),
  Y_N           (                   N + mipp::nElReg<R>() ),
  l             (L, mipp::vector<R>(N + mipp::nElReg<R>())),
  s             (L, mipp::vector<B>(N + mipp::nElReg<B>())),
  metrics_vec   (3, std::vector<R>()),
  dup_count     (L, 0),
  bit_flips     (4 * L),
  is_even       (L),
  best_path     (0),
  n_active_paths(1),
  n_array_ref   (L, std::vector<int>(m)),
  path_2_array  (L, std::vector<int>(m)),
  sorter        (N),
//sorter_simd   (N),
  best_idx      (L),
  l_tmp         (N)
{
	static_assert(API_polar::get_n_frames() == 1, "The inter-frame API_polar is not supported.");
	static_assert(sizeof(B) == sizeof(R), "Sizes of the bits and reals have to be identical.");

	assert(is_power_of_2(L));

	metrics_vec[0].resize(L * 2);
	metrics_vec[1].resize(L * 4);
	metrics_vec[2].resize((L <= 2 ? 4 : 8) * L);
}

template <typename B, typename R, class API_polar>
Decoder_polar_SCL_fast_sys<B,R,API_polar>
::~Decoder_polar_SCL_fast_sys()
{
	polar_patterns.release_patterns();
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::init_buffers()
{
	metrics[0] = std::numeric_limits<R>::min();
	std::iota(paths.begin(), paths.begin() + L, 0);

	n_active_paths = 1;

	// at the beginning, path 0 points to array 0
	std::fill(n_array_ref [0].begin(), n_array_ref [0].end(), 1);
	std::fill(path_2_array[0].begin(), path_2_array[0].end(), 0);

	for (auto i = 1; i < L; i++)
		std::fill(n_array_ref[i].begin(), n_array_ref[i].end(), 0);
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::load(const mipp::vector<R>& Y_N)
{
	std::copy(Y_N.begin(), Y_N.begin() + this->N, this->Y_N.begin());
	init_buffers();
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::hard_decode()
{
	int first_node_id = 0, off_l = 0, off_s = 0;
	recursive_decode(off_l, off_s, m, first_node_id);
	select_best_path();
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::recursive_decode(const int off_l, const int off_s, const int rev_depth, int &node_id)
{
	const int n_elmts = 1 << rev_depth;
	const int n_elm_2 = n_elmts >> 1;
	const polar_node_t node_type = polar_patterns.get_node_type(node_id);

	const bool is_terminal_pattern = (node_type == polar_node_t::RATE_0) ||
	                                 (node_type == polar_node_t::RATE_1) ||
	                                 (node_type == polar_node_t::REP)    ||
	                                 (node_type == polar_node_t::SPC);

	// root node
	if (rev_depth == m)
	{
		// f
		switch (node_type)
		{
			case STANDARD:
				API_polar::f(Y_N.data(), Y_N.data() + n_elm_2, l[0].data(), n_elm_2);
				break;
			case REP_LEFT:
				API_polar::f(Y_N.data(), Y_N.data() + n_elm_2, l[0].data(), n_elm_2);
				break;
			default:
				break;
		}

		recursive_decode(off_l, off_s, rev_depth -1, ++node_id); // recursive call left

		// g
		switch (node_type)
		{
			case STANDARD:
				for (auto i = 0; i < n_active_paths; i++)
				{
					const auto path  = paths[i];
					const auto child = l[up_ref_array_idx(path, rev_depth -1)].data();
					API_polar::g (Y_N.data(), Y_N.data() + n_elm_2, s[path].data() + off_s, child, n_elm_2);
				}
				break;
			case RATE_0_LEFT:
				for (auto i = 0; i < n_active_paths; i++)
				{
					const auto path  = paths[i];
					const auto child = l[up_ref_array_idx(path, rev_depth -1)].data();
					API_polar::g0(Y_N.data(), Y_N.data() + n_elm_2,                         child, n_elm_2);
				}
				break;
			case REP_LEFT:
				for (auto i = 0; i < n_active_paths; i++)
				{
					const auto path  = paths[i];
					const auto child = l[up_ref_array_idx(path, rev_depth -1)].data();
					API_polar::gr(Y_N.data(), Y_N.data() + n_elm_2, s[path].data() + off_s, child, n_elm_2);
				}
				break;
			default:
				break;
		}

		recursive_decode(off_l, off_s + n_elm_2, rev_depth -1, ++node_id); // recursive call right

		// xor
		switch (node_type)
		{
			case STANDARD:
				for (auto i = 0; i < n_active_paths; i++)
					API_polar::xo (s[paths[i]], off_s, off_s + n_elm_2, off_s, n_elm_2);
				break;
			case RATE_0_LEFT:
				for (auto i = 0; i < n_active_paths; i++)
					API_polar::xo0(s[paths[i]],        off_s + n_elm_2, off_s, n_elm_2);
				break;
			case REP_LEFT:
				for (auto i = 0; i < n_active_paths; i++)
					API_polar::xo (s[paths[i]], off_s, off_s + n_elm_2, off_s, n_elm_2);
				break;
			default:
				break;
		}
	}
	else if (!is_terminal_pattern && rev_depth) // other node (not root or leaf)
	{
		// f
		switch (node_type)
		{
			case STANDARD:
				for (auto i = 0; i < n_active_paths; i++)
				{
					const auto path   = paths[i];
					const auto parent = l[path_2_array    [path][rev_depth   ]].data();
					const auto child  = l[up_ref_array_idx(path, rev_depth -1)].data();
					API_polar::f(parent + off_l, parent + off_l + n_elm_2, child + off_l + n_elmts, n_elm_2);
				}
				break;
			case REP_LEFT:
				for (auto i = 0; i < n_active_paths; i++)
				{
					const auto path   = paths[i];
					const auto parent = l[path_2_array    [path][rev_depth   ]].data();
					const auto child  = l[up_ref_array_idx(path, rev_depth -1)].data();
					API_polar::f(parent + off_l, parent + off_l + n_elm_2, child + off_l + n_elmts, n_elm_2);
				}
				break;
			case RATE_0_LEFT:
				for (auto i = 0; i < n_active_paths && n_active_paths > 1; i++)
				{
					const auto path   = paths[i];
					const auto parent = l[path_2_array    [path][rev_depth   ]].data();
					const auto child  = l[up_ref_array_idx(path, rev_depth -1)].data();
					API_polar::f(parent + off_l, parent + off_l + n_elm_2, child + off_l + n_elmts, n_elm_2);
				}
				break;
			default:
				break;
		}

		recursive_decode(off_l + n_elmts, off_s, rev_depth -1, ++node_id); // recursive call left

		// g
		switch (node_type)
		{
			case STANDARD:
				for (auto i = 0; i < n_active_paths; i++)
				{
					const auto path   = paths[i];
					const auto parent = l[path_2_array    [path][rev_depth   ]].data();
					const auto child  = l[up_ref_array_idx(path, rev_depth -1)].data();
					API_polar::g (parent + off_l, parent + off_l + n_elm_2, s[path].data() + off_s, child + off_l + n_elmts, n_elm_2);
				}
				break;
			case RATE_0_LEFT:
				for (auto i = 0; i < n_active_paths; i++)
				{
					const auto path   = paths[i];
					const auto parent = l[path_2_array    [path][rev_depth   ]].data();
					const auto child  = l[up_ref_array_idx(path, rev_depth -1)].data();
					API_polar::g0(parent + off_l, parent + off_l + n_elm_2,                         child + off_l + n_elmts, n_elm_2);
				}
				break;
			case REP_LEFT:
				for (auto i = 0; i < n_active_paths; i++)
				{
					const auto path   = paths[i];
					const auto parent = l[path_2_array    [path][rev_depth   ]].data();
					const auto child  = l[up_ref_array_idx(path, rev_depth -1)].data();
					API_polar::gr(parent + off_l, parent + off_l + n_elm_2, s[path].data() + off_s, child + off_l + n_elmts, n_elm_2);
				}
				break;
			default:
				break;
		}

		recursive_decode(off_l + n_elmts, off_s + n_elm_2, rev_depth -1, ++node_id); // recursive call right

		// xor
		switch (node_type)
		{
			case STANDARD:
				for (auto i = 0; i < n_active_paths; i++)
					API_polar::xo (s[paths[i]], off_s, off_s + n_elm_2, off_s, n_elm_2);
				break;
			case RATE_0_LEFT:
				for (auto i = 0; i < n_active_paths; i++)
					API_polar::xo0(s[paths[i]],        off_s + n_elm_2, off_s, n_elm_2);
				break;
			case REP_LEFT:
				for (auto i = 0; i < n_active_paths; i++)
					API_polar::xo (s[paths[i]], off_s, off_s + n_elm_2, off_s, n_elm_2);
				break;
			default:
				break;
		}
	}
	else // leaf node
	{
		// h
		switch (node_type)
		{
			case RATE_0: update_paths_r0 (rev_depth, off_l, off_s, n_elmts); break;
			case REP:    update_paths_rep(rev_depth, off_l, off_s, n_elmts); break;
			case RATE_1: update_paths_r1 (rev_depth, off_l, off_s, n_elmts); break;
			case SPC:    update_paths_spc(rev_depth, off_l, off_s, n_elmts); break;
			default:
				break;
		}

		normalize_scl_metrics<R>(this->metrics, this->L);
	}
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::store(mipp::vector<B>& V_K) const
{
	assert(V_K.size() >= (unsigned) this->K);

	auto k = 0;
	for (auto i = 0; i < this->N; i++)
		if (!frozen_bits[i])
			V_K[k++] = s[best_path][i] ? 1 : 0;
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::store_fast(mipp::vector<B>& V) const
{
	assert(V.size() == (unsigned) this->N);
	std::copy(s[best_path].begin(), s[best_path].end() - mipp::nElReg<B>(), V.begin());
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::unpack(mipp::vector<B>& V_N) const
{
	assert(V_N.size() == frozen_bits.size());

	for (unsigned i = 0; i < V_N.size(); i++)
		V_N[i] = !frozen_bits[i] && V_N[i];
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::update_paths_r0(const int r_d, const int off_l, const int off_s, const int n_elmts)
{
	if (n_active_paths > 1)
		for (auto i = 0; i < n_active_paths; i++)
		{
			const auto path  = paths[i];
			const auto array = path_2_array[path][r_d];

			auto pen = (R)0;
			for (auto j = 0; j < n_elmts; j++)
				pen = sat_m<R>(pen + sat_m<R>(-std::min((R)l[array][off_l +j], (R)0)));
			metrics[path] = sat_m<R>(metrics[path] + pen); // add a penalty to the current path metric
		}

	if (!polar_patterns.exist_node_type(polar_node_t::RATE_0_LEFT))
		for (auto i = 0; i < n_active_paths; i++)
			std::fill(s[paths[i]].begin() + off_s, s[paths[i]].begin() + off_s + n_elmts, 0);
}

template <typename B, typename R, class API_polar>
template <int REV_D, int N_ELMTS>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::update_paths_r0(const int off_l, const int off_s)
{
	if (n_active_paths > 1)
		for (auto i = 0; i < n_active_paths; i++)
		{
			const auto path  = paths[i];
			const auto array = path_2_array[path][REV_D];

			auto pen = (R)0;
			for (auto j = 0; j < N_ELMTS; j++)
				pen = sat_m<R>(pen + sat_m<R>(-std::min((R)l[array][off_l +j], (R)0)));
			metrics[path] = sat_m<R>(metrics[path] + pen); // add a penalty to the current path metric
		}

	if (!polar_patterns.exist_node_type(polar_node_t::RATE_0_LEFT))
		for (auto i = 0; i < n_active_paths; i++)
			std::fill(s[paths[i]].begin() + off_s, s[paths[i]].begin() + off_s + N_ELMTS, 0);
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::update_paths_r1(const int r_d, const int off_l, const int off_s, const int n_elmts)
{
	if (r_d == 0)
		update_paths_rep(r_d, off_l, off_s, n_elmts);
	else
	{
		// generate the candidates with the Chase-II algorithm
		if (n_elmts == 2)
		{
			for (auto i = 0; i < n_active_paths; i++)
			{
				const auto path  = paths[i];
				const auto array = path_2_array[path][r_d];

				bit_flips[2 * path +0] = 0;
				bit_flips[2 * path +1] = 1;

				const auto pen0 = sat_m<R>(std::abs(l[array][off_l + bit_flips[2 * path +0]]));
				const auto pen1 = sat_m<R>(std::abs(l[array][off_l + bit_flips[2 * path +1]]));

				metrics_vec[1][4 * path +0] =          metrics       [    path   ];
				metrics_vec[1][4 * path +1] = sat_m<R>(metrics       [    path   ] + pen0);
				metrics_vec[1][4 * path +2] = sat_m<R>(metrics       [    path   ] + pen1);
				metrics_vec[1][4 * path +3] = sat_m<R>(metrics_vec[1][4 * path +1] + pen1);
			}
		}
		else
		{
			for (auto i = 0; i < n_active_paths; i++)
			{
				const auto path  = paths[i];
				const auto array = path_2_array[path][r_d];

				for (auto i = 0; i < n_elmts; i++) l_tmp[i] = std::abs(l[array][off_l +i]);
				sorter.partial_sort_destructive(l_tmp.data(), best_idx, n_elmts, 2);
//				sorter_simd.partial_sort_abs(l[array].data() + off_l, best_idx, n_elmts, 2);

				bit_flips[2 * path +0] = best_idx[0];
				bit_flips[2 * path +1] = best_idx[1];

				const auto pen0 = sat_m<R>(std::abs(l[array][off_l + bit_flips[2 * path +0]]));
				const auto pen1 = sat_m<R>(std::abs(l[array][off_l + bit_flips[2 * path +1]]));

				metrics_vec[1][4 * path +0] =          metrics       [    path   ];
				metrics_vec[1][4 * path +1] = sat_m<R>(metrics       [    path   ] + pen0);
				metrics_vec[1][4 * path +2] = sat_m<R>(metrics       [    path   ] + pen1);
				metrics_vec[1][4 * path +3] = sat_m<R>(metrics_vec[1][4 * path +1] + pen1);
			}
		}
		for (auto i = n_active_paths; i < L; i++)
			for (auto j = 0; j < 4; j++)
				metrics_vec[1][4 * paths[i] +j] = std::numeric_limits<R>::max();

		// L first of the lists are the L best paths
		const auto n_list = (n_active_paths * 4 >= L) ? L : n_active_paths * 4;
		sorter.partial_sort(metrics_vec[1].data(), best_idx, L * 4, n_list);

		// count the number of duplications per path
		for (auto i = 0; i < n_list; i++)
			dup_count[best_idx[i] / 4]++;

		// erase bad paths
		erase_bad_paths();

		for (auto i = 0; i < n_list; i++)
		{
			const auto path  = best_idx[i] / 4;
			const auto dup   = best_idx[i] % 4;
			const auto array = path_2_array[path][r_d];

			API_polar::h(s[path], l[array], off_l, off_s, n_elmts);

			const auto new_path = (dup_count[path] > 1) ? duplicate_tree(path, off_l, off_s, n_elmts) : path;
			flip_bits_r1(path, new_path, dup, off_s, n_elmts);
			metrics[new_path] = metrics_vec[1][best_idx[i]];

			dup_count[path]--;
		}
	}
}

template <typename B, typename R, class API_polar>
template <int REV_D, int N_ELMTS>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::update_paths_r1(const int off_l, const int off_s)
{
	if (REV_D == 0)
		update_paths_rep<REV_D, N_ELMTS>(off_l, off_s);
	else
	{
		// generate the candidates with the Chase-II algorithm
		if (N_ELMTS == 2)
		{
			for (auto i = 0; i < n_active_paths; i++)
			{
				const auto path  = paths[i];
				const auto array = path_2_array[path][REV_D];

				bit_flips[2 * path +0] = 0;
				bit_flips[2 * path +1] = 1;

				const auto pen0 = sat_m<R>(std::abs(l[array][off_l + bit_flips[2 * path +0]]));
				const auto pen1 = sat_m<R>(std::abs(l[array][off_l + bit_flips[2 * path +1]]));

				metrics_vec[1][4 * path +0] =          metrics       [    path   ];
				metrics_vec[1][4 * path +1] = sat_m<R>(metrics       [    path   ] + pen0);
				metrics_vec[1][4 * path +2] = sat_m<R>(metrics       [    path   ] + pen1);
				metrics_vec[1][4 * path +3] = sat_m<R>(metrics_vec[1][4 * path +1] + pen1);
			}
		}
		else
		{
			for (auto i = 0; i < n_active_paths; i++)
			{
				const auto path  = paths[i];
				const auto array = path_2_array[path][REV_D];

				for (auto i = 0; i < N_ELMTS; i++) l_tmp[i] = std::abs(l[array][off_l +i]);
				sorter.partial_sort_destructive(l_tmp.data(), best_idx, N_ELMTS, 2);
//				sorter_simd.partial_sort_abs(l[array].data() + off_l, best_idx, N_ELMTS, 2);

				bit_flips[2 * path +0] = best_idx[0];
				bit_flips[2 * path +1] = best_idx[1];

				const auto pen0 = sat_m<R>(std::abs(l[array][off_l + bit_flips[2 * path +0]]));
				const auto pen1 = sat_m<R>(std::abs(l[array][off_l + bit_flips[2 * path +1]]));

				metrics_vec[1][4 * path +0] =          metrics       [    path   ];
				metrics_vec[1][4 * path +1] = sat_m<R>(metrics       [    path   ] + pen0);
				metrics_vec[1][4 * path +2] = sat_m<R>(metrics       [    path   ] + pen1);
				metrics_vec[1][4 * path +3] = sat_m<R>(metrics_vec[1][4 * path +1] + pen1);
			}
		}
		for (auto i = n_active_paths; i < L; i++)
			for (auto j = 0; j < 4; j++)
				metrics_vec[1][4 * paths[i] +j] = std::numeric_limits<R>::max();

		// L first of the lists are the L best paths
		const auto n_list = (n_active_paths * 4 >= L) ? L : n_active_paths * 4;
		sorter.partial_sort(metrics_vec[1].data(), best_idx, L * 4, n_list);

		// count the number of duplications per path
		for (auto i = 0; i < n_list; i++)
			dup_count[best_idx[i] / 4]++;

		// erase bad paths
		erase_bad_paths();

		for (auto i = 0; i < n_list; i++)
		{
			const auto path  = best_idx[i] / 4;
			const auto dup   = best_idx[i] % 4;
			const auto array = path_2_array[path][REV_D];

			API_polar::template h<N_ELMTS>(s[path], l[array], off_l, off_s, N_ELMTS);

			const auto new_path = (dup_count[path] > 1) ? duplicate_tree(path, off_l, off_s, N_ELMTS) : path;
			flip_bits_r1(path, new_path, dup, off_s, N_ELMTS);
			metrics[new_path] = metrics_vec[1][best_idx[i]];

			dup_count[path]--;
		}
	}
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::flip_bits_r1(const int old_path, const int new_path, const int dup, const int off_s, const int n_elmts)
{
	constexpr B b = bit_init<B>();

	switch (dup)
	{
	case 0:
		// nothing to do
		break;
	case 1:
		s[new_path][off_s + bit_flips[2 * old_path +0]] = !s[old_path][off_s + bit_flips[2 * old_path +0]] ? b : 0;
		break;
	case 2:
		s[new_path][off_s + bit_flips[2 * old_path +1]] = !s[old_path][off_s + bit_flips[2 * old_path +1]] ? b : 0;
		break;
	case 3:
		s[new_path][off_s + bit_flips[2 * old_path +0]] = !s[old_path][off_s + bit_flips[2 * old_path +0]] ? b : 0;
		s[new_path][off_s + bit_flips[2 * old_path +1]] = !s[old_path][off_s + bit_flips[2 * old_path +1]] ? b : 0;
		break;
	default:
		std::cout << bold_red("(EE) Flip bits error on rate 1 node.") << std::endl;
		std::exit(-1);
		break;
	}
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::update_paths_rep(const int r_d, const int off_l, const int off_s, const int n_elmts)
{
	constexpr B b = bit_init<B>();

	// generate the two possible candidates
	for (auto i = 0; i < n_active_paths; i++)
	{
		const auto path  = paths[i];
		const auto array = path_2_array[path][r_d];

		auto pen0 = (R)0;
		auto pen1 = (R)0;
		for (auto j = 0; j < n_elmts; j++)
		{
			pen0 = sat_m<R>(pen0 + sat_m<R>(-std::min(l[array][off_l +j], (R)0)));
			pen1 = sat_m<R>(pen1 + sat_m<R>(+std::max(l[array][off_l +j], (R)0)));
		}
		metrics_vec[0][2 * path +0] = sat_m<R>(metrics[path] + pen0);
		metrics_vec[0][2 * path +1] = sat_m<R>(metrics[path] + pen1);
	}

	if (n_active_paths <= L / 2)
	{
		const auto n_active_paths_cpy = n_active_paths;
		for (auto i = 0; i < n_active_paths_cpy; i++)
		{
			const auto path = paths[i];
			const auto new_path = duplicate_tree(path, off_l, off_s, n_elmts);

			std::fill(s[    path].begin() + off_s, s[    path].begin() + off_s + n_elmts, 0);
			std::fill(s[new_path].begin() + off_s, s[new_path].begin() + off_s + n_elmts, b);

			metrics[    path] = metrics_vec[0][2 * path +0];
			metrics[new_path] = metrics_vec[0][2 * path +1];
		}
	}
	else // n_active_paths == L
	{
		// sort hypothetic metrics
		sorter.partial_sort(metrics_vec[0].data(), best_idx, L * 2, L);

		// count the number of duplications per path
		for (auto i = 0; i < L; i++)
			dup_count[best_idx[i] / 2]++;

		// erase bad paths
		erase_bad_paths();

		// duplicate paths
		for (auto path = 0; path < L; path++)
		{
			if (dup_count[path] == 1)
			{
				const auto comp = metrics_vec[0][2 * path +0] > metrics_vec[0][2 * path +1];
				std::fill(s[path].begin() + off_s, s[path].begin() + off_s + n_elmts, comp ? b : 0);

				metrics[path] = metrics_vec[0][2 * path + (comp ? 1 : 0)];
			}
			else if (dup_count[path] == 2)
			{
				const auto new_path = duplicate_tree(path, off_l, off_s, n_elmts);
				std::fill(s[    path].begin() + off_s, s[    path].begin() + off_s + n_elmts, 0);
				std::fill(s[new_path].begin() + off_s, s[new_path].begin() + off_s + n_elmts, b);

				metrics[    path] = metrics_vec[0][2 * path +0];
				metrics[new_path] = metrics_vec[0][2 * path +1];
			}

			dup_count[path] = 0;
		}
	}
}

template <typename B, typename R, class API_polar>
template <int REV_D, int N_ELMTS>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::update_paths_rep(const int off_l, const int off_s)
{
	constexpr B b = bit_init<B>();

	// generate the two possible candidates
	for (auto i = 0; i < n_active_paths; i++)
	{
		const auto path  = paths[i];
		const auto array = path_2_array[path][REV_D];

		auto pen0 = (R)0;
		auto pen1 = (R)0;
		for (auto j = 0; j < N_ELMTS; j++)
		{
			pen0 = sat_m<R>(pen0 + sat_m<R>(-std::min(l[array][off_l +j], (R)0)));
			pen1 = sat_m<R>(pen1 + sat_m<R>(+std::max(l[array][off_l +j], (R)0)));
		}
		metrics_vec[0][2 * path +0] = sat_m<R>(metrics[path] + pen0);
		metrics_vec[0][2 * path +1] = sat_m<R>(metrics[path] + pen1);
	}

	if (n_active_paths <= L / 2)
	{
		const auto n_active_paths_cpy = n_active_paths;
		for (auto i = 0; i < n_active_paths_cpy; i++)
		{
			const auto path = paths[i];
			const auto new_path = duplicate_tree(path, off_l, off_s, N_ELMTS);

			std::fill(s[    path].begin() + off_s, s[    path].begin() + off_s + N_ELMTS, 0);
			std::fill(s[new_path].begin() + off_s, s[new_path].begin() + off_s + N_ELMTS, b);

			metrics[    path] = metrics_vec[0][2 * path +0];
			metrics[new_path] = metrics_vec[0][2 * path +1];
		}
	}
	else // n_active_paths == L
	{
		// sort hypothetic metrics
		sorter.partial_sort(metrics_vec[0].data(), best_idx, L * 2, L);

		// count the number of duplications per path
		for (auto i = 0; i < L; i++)
			dup_count[best_idx[i] / 2]++;

		// erase bad paths
		erase_bad_paths();

		// duplicate paths
		for (auto path = 0; path < L; path++)
		{
			if (dup_count[path] == 1)
			{
				const auto comp = metrics_vec[0][2 * path +0] > metrics_vec[0][2 * path +1];
				std::fill(s[path].begin() + off_s, s[path].begin() + off_s + N_ELMTS, comp ? b : 0);

				metrics[path] = metrics_vec[0][2 * path + (comp ? 1 : 0)];
			}
			else if (dup_count[path] == 2)
			{
				const auto new_path = duplicate_tree(path, off_l, off_s, N_ELMTS);
				std::fill(s[    path].begin() + off_s, s[    path].begin() + off_s + N_ELMTS, 0);
				std::fill(s[new_path].begin() + off_s, s[new_path].begin() + off_s + N_ELMTS, b);

				metrics[    path] = metrics_vec[0][2 * path +0];
				metrics[new_path] = metrics_vec[0][2 * path +1];
			}

			dup_count[path] = 0;
		}
	}
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::update_paths_spc(const int r_d, const int off_l, const int off_s, const int n_elmts)
{
	// the number of candidates to generate per list
	const auto n_cands = L <= 2 ? 4 : 8;

	// generate the candidates with the Chase-II algorithm
	if (n_elmts == 4)
	{
		for (auto i = 0; i < n_active_paths; i++)
		{
			const auto path  = paths[i];
			const auto array = path_2_array[paths[i]][r_d];

			for (auto j = 0; j < 4; j++)
				bit_flips[4 * path +j] = j;

			auto sum = 0;
			for (auto j = 0; j < n_elmts; j++)
				sum ^= (l[array][off_l +j] < 0);
			is_even[path] = (sum == 0);

			const auto pen0 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +0]]));
			const auto pen1 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +1]]));
			const auto pen2 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +2]]));
			const auto pen3 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +3]]));

			metrics_vec[2][n_cands * path +0] =          sat_m<R>(metrics[path] + (!is_even[path] ? pen0 : 0));
			metrics_vec[2][n_cands * path +1] = sat_m<R>(sat_m<R>(metrics[path] + ( is_even[path] ? pen0 : 0)) + pen1);
			metrics_vec[2][n_cands * path +2] = sat_m<R>(sat_m<R>(metrics[path] + ( is_even[path] ? pen0 : 0)) + pen2);
			metrics_vec[2][n_cands * path +3] = sat_m<R>(sat_m<R>(metrics[path] + ( is_even[path] ? pen0 : 0)) + pen3);

			if (L > 2)
			{
				metrics_vec[2][n_cands * path +4] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +0] + pen1) + pen2);
				metrics_vec[2][n_cands * path +5] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +0] + pen1) + pen3);
				metrics_vec[2][n_cands * path +6] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +0] + pen2) + pen3);
				metrics_vec[2][n_cands * path +7] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +1] + pen2) + pen3);
			}
		}
	}
	else
	{
		for (auto i = 0; i < n_active_paths; i++)
		{
			const auto path  = paths[i];
			const auto array = path_2_array[paths[i]][r_d];

			for (auto i = 0; i < n_elmts; i++) l_tmp[i] = std::abs(l[array][off_l +i]);
			sorter.partial_sort_destructive(l_tmp.data(), best_idx, n_elmts, 4);
//			sorter_simd.partial_sort_abs(l[array].data() + off_l, best_idx, n_elmts, 4);

			for (auto j = 0; j < 4; j++)
				bit_flips[4 * path +j] = best_idx[j];

			auto sum = 0;
			for (auto j = 0; j < n_elmts; j++)
				sum ^= (l[array][off_l +j] < 0);
			is_even[path] = (sum == 0);

			const auto pen0 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +0]]));
			const auto pen1 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +1]]));
			const auto pen2 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +2]]));
			const auto pen3 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +3]]));

			metrics_vec[2][n_cands * path +0] =          sat_m<R>(metrics[path] + (!is_even[path] ? pen0 : 0));
			metrics_vec[2][n_cands * path +1] = sat_m<R>(sat_m<R>(metrics[path] + ( is_even[path] ? pen0 : 0)) + pen1);
			metrics_vec[2][n_cands * path +2] = sat_m<R>(sat_m<R>(metrics[path] + ( is_even[path] ? pen0 : 0)) + pen2);
			metrics_vec[2][n_cands * path +3] = sat_m<R>(sat_m<R>(metrics[path] + ( is_even[path] ? pen0 : 0)) + pen3);

			if (L > 2)
			{
				metrics_vec[2][n_cands * path +4] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +0] + pen1) + pen2);
				metrics_vec[2][n_cands * path +5] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +0] + pen1) + pen3);
				metrics_vec[2][n_cands * path +6] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +0] + pen2) + pen3);
				metrics_vec[2][n_cands * path +7] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +1] + pen2) + pen3);
			}
		}
	}
	for (auto i = n_active_paths; i < L; i++)
		for (auto j = 0; j < n_cands; j++)
			metrics_vec[2][n_cands * paths[i] +j] = std::numeric_limits<R>::max();

	// L first of the lists are the L best paths
	const auto n_list = (n_active_paths * n_cands >= L) ? L : n_active_paths * n_cands;
	sorter.partial_sort(metrics_vec[2].data(), best_idx, n_cands * L, n_list);

	// count the number of duplications per path
	for (auto i = 0; i < n_list; i++)
		dup_count[best_idx[i] / n_cands]++;

	// erase bad paths
	erase_bad_paths();

	for (auto i = 0; i < n_list; i++)
	{
		const auto path  = best_idx[i] / n_cands;
		const auto dup   = best_idx[i] % n_cands;
		const auto array = path_2_array[path][r_d];

		API_polar::h(s[path], l[array], off_l, off_s, n_elmts);

		const auto new_path = (dup_count[path] > 1) ? duplicate_tree(path, off_l, off_s, n_elmts) : path;
		flip_bits_spc(path, new_path, dup, off_s, n_elmts);
		metrics[new_path] = metrics_vec[2][best_idx[i]];

		dup_count[path]--;
	}
}

template <typename B, typename R, class API_polar>
template <int REV_D, int N_ELMTS>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::update_paths_spc(const int off_l, const int off_s)
{
	// the number of candidates to generate per list
	const auto n_cands = L <= 2 ? 4 : 8;

	// generate the candidates with the Chase-II algorithm
	if (N_ELMTS == 4)
	{
		for (auto i = 0; i < n_active_paths; i++)
		{
			const auto path  = paths[i];
			const auto array = path_2_array[paths[i]][REV_D];

			for (auto j = 0; j < 4; j++)
				bit_flips[4 * path +j] = j;

			auto sum = 0;
			for (auto j = 0; j < N_ELMTS; j++)
				sum ^= (l[array][off_l +j] < 0);
			is_even[path] = (sum == 0);

			const auto pen0 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +0]]));
			const auto pen1 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +1]]));
			const auto pen2 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +2]]));
			const auto pen3 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +3]]));

			metrics_vec[2][n_cands * path +0] =          sat_m<R>(metrics[path] + (!is_even[path] ? pen0 : 0));
			metrics_vec[2][n_cands * path +1] = sat_m<R>(sat_m<R>(metrics[path] + ( is_even[path] ? pen0 : 0)) + pen1);
			metrics_vec[2][n_cands * path +2] = sat_m<R>(sat_m<R>(metrics[path] + ( is_even[path] ? pen0 : 0)) + pen2);
			metrics_vec[2][n_cands * path +3] = sat_m<R>(sat_m<R>(metrics[path] + ( is_even[path] ? pen0 : 0)) + pen3);

			if (L > 2)
			{
				metrics_vec[2][n_cands * path +4] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +0] + pen1) + pen2);
				metrics_vec[2][n_cands * path +5] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +0] + pen1) + pen3);
				metrics_vec[2][n_cands * path +6] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +0] + pen2) + pen3);
				metrics_vec[2][n_cands * path +7] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +1] + pen2) + pen3);
			}
		}
	}
	else
	{
		for (auto i = 0; i < n_active_paths; i++)
		{
			const auto path  = paths[i];
			const auto array = path_2_array[paths[i]][REV_D];

			for (auto i = 0; i < N_ELMTS; i++) l_tmp[i] = std::abs(l[array][off_l +i]);
			sorter.partial_sort_destructive(l_tmp.data(), best_idx, N_ELMTS, 4);
//			sorter_simd.partial_sort_abs(l[array].data() + off_l, best_idx, N_ELMTS, 4);

			for (auto j = 0; j < 4; j++)
				bit_flips[4 * path +j] = best_idx[j];

			auto sum = 0;
			for (auto j = 0; j < N_ELMTS; j++)
				sum ^= (l[array][off_l +j] < 0);
			is_even[path] = (sum == 0);

			const auto pen0 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +0]]));
			const auto pen1 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +1]]));
			const auto pen2 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +2]]));
			const auto pen3 = sat_m<R>(std::abs(l[array][off_l + bit_flips[4 * path +3]]));

			metrics_vec[2][n_cands * path +0] =          sat_m<R>(metrics[path] + (!is_even[path] ? pen0 : 0));
			metrics_vec[2][n_cands * path +1] = sat_m<R>(sat_m<R>(metrics[path] + ( is_even[path] ? pen0 : 0)) + pen1);
			metrics_vec[2][n_cands * path +2] = sat_m<R>(sat_m<R>(metrics[path] + ( is_even[path] ? pen0 : 0)) + pen2);
			metrics_vec[2][n_cands * path +3] = sat_m<R>(sat_m<R>(metrics[path] + ( is_even[path] ? pen0 : 0)) + pen3);

			if (L > 2)
			{
				metrics_vec[2][n_cands * path +4] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +0] + pen1) + pen2);
				metrics_vec[2][n_cands * path +5] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +0] + pen1) + pen3);
				metrics_vec[2][n_cands * path +6] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +0] + pen2) + pen3);
				metrics_vec[2][n_cands * path +7] = sat_m<R>(sat_m<R>(metrics_vec[2][n_cands * path +1] + pen2) + pen3);
			}
		}
	}
	for (auto i = n_active_paths; i < L; i++)
		for (auto j = 0; j < n_cands; j++)
			metrics_vec[2][n_cands * paths[i] +j] = std::numeric_limits<R>::max();

	// L first of the lists are the L best paths
	const auto n_list = (n_active_paths * n_cands >= L) ? L : n_active_paths * n_cands;
	sorter.partial_sort(metrics_vec[2].data(), best_idx, n_cands * L, n_list);

	// count the number of duplications per path
	for (auto i = 0; i < n_list; i++)
		dup_count[best_idx[i] / n_cands]++;

	// erase bad paths
	erase_bad_paths();

	for (auto i = 0; i < n_list; i++)
	{
		const auto path  = best_idx[i] / n_cands;
		const auto dup   = best_idx[i] % n_cands;
		const auto array = path_2_array[path][REV_D];

		API_polar::template h<N_ELMTS>(s[path], l[array], off_l, off_s, N_ELMTS);

		const auto new_path = (dup_count[path] > 1) ? duplicate_tree(path, off_l, off_s, N_ELMTS) : path;
		flip_bits_spc(path, new_path, dup, off_s, N_ELMTS);
		metrics[new_path] = metrics_vec[2][best_idx[i]];

		dup_count[path]--;
	}
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::flip_bits_spc(const int old_path, const int new_path, const int dup, const int off_s, const int n_elmts)
{
	constexpr B b = bit_init<B>();

	switch(dup)
	{
	case 0 :
		if (!is_even[old_path])
			s[new_path][off_s + bit_flips[4 * old_path +0]] = s[old_path][off_s + bit_flips[4 * old_path +0]] ? 0 : b;
		break;
	case 1 :
		if (is_even[old_path])
			s[new_path][off_s + bit_flips[4 * old_path +0]] = s[old_path][off_s + bit_flips[4 * old_path +0]] ? 0 : b;
			s[new_path][off_s + bit_flips[4 * old_path +1]] = s[old_path][off_s + bit_flips[4 * old_path +1]] ? 0 : b;
		break;
	case 2 :
		if (is_even[old_path])
			s[new_path][off_s + bit_flips[4 * old_path +0]] = s[old_path][off_s + bit_flips[4 * old_path +0]] ? 0 : b;
			s[new_path][off_s + bit_flips[4 * old_path +2]] = s[old_path][off_s + bit_flips[4 * old_path +2]] ? 0 : b;
		break;
	case 3 :
		if (is_even[old_path])
			s[new_path][off_s + bit_flips[4 * old_path +0]] = s[old_path][off_s + bit_flips[4 * old_path +0]] ? 0 : b;
			s[new_path][off_s + bit_flips[4 * old_path +3]] = s[old_path][off_s + bit_flips[4 * old_path +3]] ? 0 : b;
		break;
	case 4 :
		if (!is_even[old_path])
			s[new_path][off_s + bit_flips[4 * old_path +0]] = s[old_path][off_s + bit_flips[4 * old_path +0]] ? 0 : b;
			s[new_path][off_s + bit_flips[4 * old_path +1]] = s[old_path][off_s + bit_flips[4 * old_path +1]] ? 0 : b;
			s[new_path][off_s + bit_flips[4 * old_path +2]] = s[old_path][off_s + bit_flips[4 * old_path +2]] ? 0 : b;
		break;
	case 5 :
		if (!is_even[old_path])
			s[new_path][off_s + bit_flips[4 * old_path +0]] = s[old_path][off_s + bit_flips[4 * old_path +0]] ? 0 : b;
			s[new_path][off_s + bit_flips[4 * old_path +1]] = s[old_path][off_s + bit_flips[4 * old_path +1]] ? 0 : b;
			s[new_path][off_s + bit_flips[4 * old_path +3]] = s[old_path][off_s + bit_flips[4 * old_path +3]] ? 0 : b;
		break;
	case 6 :
		if (!is_even[old_path])
			s[new_path][off_s + bit_flips[4 * old_path +0]] = s[old_path][off_s + bit_flips[4 * old_path +0]] ? 0 : b;
			s[new_path][off_s + bit_flips[4 * old_path +2]] = s[old_path][off_s + bit_flips[4 * old_path +2]] ? 0 : b;
			s[new_path][off_s + bit_flips[4 * old_path +3]] = s[old_path][off_s + bit_flips[4 * old_path +3]] ? 0 : b;
		break;
	case 7 :
		if (is_even[old_path])
			s[new_path][off_s + bit_flips[4 * old_path +0]] = s[old_path][off_s + bit_flips[4 * old_path +0]] ? 0 : b;
			s[new_path][off_s + bit_flips[4 * old_path +1]] = s[old_path][off_s + bit_flips[4 * old_path +1]] ? 0 : b;
			s[new_path][off_s + bit_flips[4 * old_path +2]] = s[old_path][off_s + bit_flips[4 * old_path +2]] ? 0 : b;
			s[new_path][off_s + bit_flips[4 * old_path +3]] = s[old_path][off_s + bit_flips[4 * old_path +3]] ? 0 : b;
		break;
	default:
		std::cout << bold_red("(EE) Flip bits error on SPC node.") << std::endl;
		std::exit(-1);
		break;
	}
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::delete_path(int path_id)
{
	const auto old_path = paths[path_id];
	for (auto i = 0; i < m; i++)
		n_array_ref[path_2_array[old_path][i]][i]--;

	paths[path_id] = paths[--n_active_paths];
	paths[n_active_paths] = old_path;
}

template <typename B, typename R, class API_polar>
int Decoder_polar_SCL_fast_sys<B,R,API_polar>
::select_best_path()
{
	best_path = -1;
	for (auto i = 0; i < n_active_paths; i++)
		if (best_path == -1 || metrics[paths[i]] < metrics[best_path])
			best_path = paths[i];

	if (best_path == -1)
		best_path = 0;

	return n_active_paths;
}

template <typename B, typename R, class API_polar>
int Decoder_polar_SCL_fast_sys<B,R,API_polar>
::up_ref_array_idx(const int path, const int r_d)
{
	auto old_array = path_2_array[path][r_d];

	// if more than 1 path points to the array
	if (n_array_ref[old_array][r_d] > 1)
	{
		// allocate new array to given path, r_d
		n_array_ref[old_array][r_d]--;

		auto new_array = 0;
		while (n_array_ref[new_array][r_d]) new_array++;

		path_2_array[path     ][r_d] = new_array;
		n_array_ref [new_array][r_d]++;

		return new_array;
	}

	return old_array;
}

template <typename B, typename R, class API_polar>
void Decoder_polar_SCL_fast_sys<B,R,API_polar>
::erase_bad_paths()
{
	// erase bad paths
	auto k = 0;
	auto n_active_paths_cpy = n_active_paths;
	for (auto i = 0; i < n_active_paths_cpy; i++)
		if (dup_count[paths[k]] == 0)
			delete_path(k);
		else
			k++;
}

template <typename B, typename R, class API_polar>
int Decoder_polar_SCL_fast_sys<B,R,API_polar>
::duplicate_tree(const int old_path, const int off_l, const int off_s, const int n_elmts)
{
	const auto new_path = paths[n_active_paths++];

//	std::copy(path_2_array[old_path].begin(), path_2_array[old_path].end(), path_2_array[new_path].begin());
	const auto loop_size = (int)path_2_array[old_path].size();
	for (auto i = 0; i < loop_size; i++)
		path_2_array[new_path][i] = path_2_array[old_path][i];

	for (auto i = 0; i < m; i++)
		n_array_ref[path_2_array[new_path][i]][i]++;

//	std::copy(s[old_path].begin(), s[old_path].begin() + off_s + n_elmts, s[new_path].begin());
	for (auto i = 0; i < off_s + n_elmts; i++)
		s[new_path][i] = s[old_path][i]; // TODO: this line takes a big part of the decoding time!

	return new_path;
}
