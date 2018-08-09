#ifndef FACTORY_CODEC_RS_HPP
#define FACTORY_CODEC_RS_HPP

#include <string>
#include <cmath>

#include "Factory/Module/Encoder/RS/Encoder_RS.hpp"
#include "Factory/Module/Decoder/RS/Decoder_RS.hpp"

#include "Module/Codec/RS/Codec_RS.hpp"

#include "../Codec_SIHO_HIHO.hpp"

namespace aff3ct
{
namespace factory
{
extern const std::string Codec_RS_name;
extern const std::string Codec_RS_prefix;
struct Codec_RS : public Codec_SIHO_HIHO
{
	class parameters : public Codec_SIHO_HIHO::parameters
	{
	public:
		// ------------------------------------------------------------------------------------------------- PARAMETERS
		// depending parameters
		Encoder_RS::parameters *enc;
		Decoder_RS::parameters *dec;

		// ---------------------------------------------------------------------------------------------------- METHODS
		explicit parameters(const std::string &p = Codec_RS_prefix);
		virtual ~parameters();
		Codec_RS::parameters* clone() const;

		// parameters construction
		void get_description(tools::Argument_map_info &args) const;
		void store          (const tools::Argument_map_value &vals);
		void get_headers    (std::map<std::string,header_list>& headers, const bool full = true) const;

		// builder
		template <typename B = int, typename Q = float>
		module::Codec_RS<B,Q>* build(module::CRC<B> *crc = nullptr) const;
	};

	template <typename B = int, typename Q = float>
	static module::Codec_RS<B,Q>* build(const parameters &params, module::CRC<B> *crc = nullptr);
};
}
}

#endif /* FACTORY_CODEC_RS_HPP */