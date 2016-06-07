#ifndef DECODER_POLAR_SCL_NAIVE_CA_SYS_
#define DECODER_POLAR_SCL_NAIVE_CA_SYS_

template <typename B, typename R, proto_f<R> F, proto_g<B,R> G>
class Decoder_polar_SCL_naive_CA_sys : public Decoder_polar_SCL_naive_CA<B,R,F,G>
{

public:
	Decoder_polar_SCL_naive_CA_sys(const int& N, const int& L, const mipp::vector<B>& frozen_bits, CRC<B>& crc);

protected:
	virtual void select_best_path();
	virtual void store (mipp::vector<B>& V_N      ) const;
};

#include "Decoder_polar_SCL_naive_CA_sys.hxx"

#endif /* DECODER_POLAR_SCL_NAIVE_CA_SYS_ */
