#ifndef CHANNEL_OPTICAL_HPP_
#define CHANNEL_OPTICAL_HPP_

#include "Tools/Algo/Noise_generator/Noise_generator.hpp"

#include "../Channel.hpp"

namespace aff3ct
{
namespace module
{

/* Optical channel is for optical communications.
 * There is a specific noise applied on sent bit at 0, and another one to 1.
 * The sigma variable from the inherited class represents the received optical power (ROP)
*/

template <typename R = float>
class Channel_optical : public Channel<R>
{
protected:
	tools::Noise_generator<R> *noise_generator_p0;
	tools::Noise_generator<R> *noise_generator_p1;

public:
	Channel_optical(const int N,
	                tools::Noise_generator<R> *noise_generator_p0,
	                tools::Noise_generator<R> *noise_generator_p1,
	                const R ROP = (R)1, const int n_frames = 1);

	virtual ~Channel_optical();

	void _add_noise(const R *X_N, R *Y_N, const int frame_id = -1);
	virtual void set_sigma(const R ROP);
};
}
}

#endif /* CHANNEL_OPTICAL_HPP_ */
