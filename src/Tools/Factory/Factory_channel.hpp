#ifndef FACTORY_CHANNEL_HPP
#define FACTORY_CHANNEL_HPP

#include "../../Channel/Channel.hpp"

#include "../params.h"

template <typename B, typename R>
struct Factory_channel
{
	static Channel<B,R>* build(const t_channel_param &chan_params, const R& sigma, const int seed = 0, 
	                           const R scaling_factor = 1);
};

#endif /* FACTORY_CHANNEL_HPP */