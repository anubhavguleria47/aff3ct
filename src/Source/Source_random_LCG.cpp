#include <cstdlib>

#include "Source_random_LCG.hpp"

template <typename B>
Source_random_LCG<B>
::Source_random_LCG() 
: g_seed(rand()) 
{
}

template <typename B>
Source_random_LCG<B>
::~Source_random_LCG()
{
}

template <typename B>
void Source_random_LCG<B>
::generate(mipp::vector<B>& U_K)
{
	auto size = U_K.size();
	// generate a random k bits vector U_k
	for (unsigned i = 0; i < size; i++)
		U_K[i] = random_number() & 0x1;
}

template <typename B>
int Source_random_LCG<B>
::random_number()
{
	// Linear Congruential Generator (LCG) algorithm: 
	// https://software.intel.com/en-us/articles/fast-random-number-generator-on-the-intel-pentiumr-4-processor/
	g_seed = (214013 * g_seed + 2531011);
	return (g_seed >> 16) & 0x7FFF;
}

template <typename B>
int Source_random_LCG<B>
::rand_max()
{
	return 32767;
}

// ==================================================================================== explicit template instantiation 
#include "../Tools/types.h"
#ifdef MULTI_PREC
template class Source_random_LCG<B_8>;
template class Source_random_LCG<B_16>;
template class Source_random_LCG<B_32>;
template class Source_random_LCG<B_64>;
#else
template class Source_random_LCG<B>;
#endif
// ==================================================================================== explicit template instantiation