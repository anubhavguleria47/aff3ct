#ifndef FACTORY_ERROR_ANALYZER_HPP
#define FACTORY_ERROR_ANALYZER_HPP

#include "../../Error/Error_analyzer.hpp"

#include "../params.h"

template <typename B, typename R>
struct Factory_error_analyzer
{
	static Error_analyzer<B,R>* build(const t_simulation_param &simu_params, 
	                                  const t_code_param &code_params, 
	                                  const int n_frames = 1);
};

#endif /* FACTORY_ERROR_ANALYZER_HPP */