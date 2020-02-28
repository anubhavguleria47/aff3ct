/*!
 * \file
 * \brief Class module::Probe.
 */
#ifndef PROBE_HPP_
#define PROBE_HPP_

#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <typeindex>

#include "Module/Task.hpp"
#include "Module/Socket.hpp"
#include "Module/Module.hpp"
#include "Tools/Interface/Interface_reset.hpp"
#include "Tools/Display/Reporter/Probe/Reporter_probe.hpp"

namespace aff3ct
{
namespace module
{
	namespace prb
	{
		enum class tsk : uint8_t { probe, SIZE };

		namespace sck
		{
			enum class probe : uint8_t { in, status };
		}
	}

template <typename T>
class Probe : public Module, public tools::Interface_reset
{
public:
	inline Task&   operator[](const prb::tsk        t);
	inline Socket& operator[](const prb::sck::probe s);

protected:
	const int size;
	const std::string col_name;
	tools::Reporter_probe& reporter;

public:
	Probe(const int size, const std::string &col_name, tools::Reporter_probe& reporter, const int n_frames = 1);

	virtual ~Probe() = default;

	template <class AT = std::allocator<T>>
	void probe(const std::vector<T,AT>& in, const int frame_id = -1);

	virtual void probe(const T *in, const int frame_id = -1);

	virtual std::type_index get_datatype() const = 0;

	virtual void reset();

protected:
	virtual void _probe(const T *in, const int frame_id);
};
}
}

#ifndef DOXYGEN_SHOULD_SKIP_THIS
#include "Module/Probe/Probe.hxx"
#endif

#endif /* PROBE_HPP_ */
