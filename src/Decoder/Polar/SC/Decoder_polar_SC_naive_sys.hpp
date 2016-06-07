#ifndef DECODER_POLAR_SC_NAIVE_SYS_
#define DECODER_POLAR_SC_NAIVE_SYS_

#include <vector>
#include "../../../Tools/MIPP/mipp.h"

#include "Decoder_polar_SC_naive.hpp"
#include "../decoder_polar_functions.h"
#include "../../../Tools/Tree/Binary_tree.hpp"

template <typename B, typename R, proto_f<R> F, proto_g<B,R> G, proto_h<B,R> H>
class Decoder_polar_SC_naive_sys : public Decoder_polar_SC_naive<B,R,F,G,H>
{
public:
	Decoder_polar_SC_naive_sys(const int& N, const mipp::vector<B>& frozen_bits);
	virtual ~Decoder_polar_SC_naive_sys();

	void store(mipp::vector<B>& V_N) const;
};

#include "Decoder_polar_SC_naive_sys.hxx"

#endif /* DECODER_POLAR_SC_NAIVE_SYS_ */
