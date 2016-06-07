#ifndef QUANTIZER_TRICKY_HPP_
#define QUANTIZER_TRICKY_HPP_

#include <vector>
#include "../Tools/MIPP/mipp.h"

#include "Quantizer.hpp"

template <typename R, typename Q>
class Quantizer_tricky : public Quantizer<R,Q>
{
private:
	const int val_max;
	const int val_min;
	R delta_inv;
	const R& sigma;

public:
	Quantizer_tricky(const R& sigma);
	Quantizer_tricky(const short& saturation_pos, const R& sigma);
	Quantizer_tricky(const float min_max, const R& sigma);
	Quantizer_tricky(const float min_max, const short& saturation_pos, const R& sigma);
	virtual ~Quantizer_tricky();

	void process(mipp::vector<R>& Y_N1, mipp::vector<Q>& Y_N2);

private:
	inline R saturate(R val) const;
};

#endif /* QUANTIZER_TRICKY_HPP_ */

