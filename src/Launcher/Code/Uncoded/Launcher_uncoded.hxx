#include <iostream>

#include "Tools/Codec/Uncoded/Codec_uncoded.hpp"

#include "Launcher_uncoded.hpp"

namespace aff3ct
{
namespace launcher
{
template <class C, typename B, typename R, typename Q>
Launcher_uncoded<C,B,R,Q>
::Launcher_uncoded(const int argc, const char **argv, std::ostream &stream)
: C(argc, argv, stream)
{
	params_enc = new tools::Factory_encoder   ::parameters();
	params_dec = new tools::Factory_decoder_NO::parameters();

	if (this->params->enc != nullptr) { delete this->params->enc; this->params->enc = params_enc; }
	if (this->params->dec != nullptr) { delete this->params->dec; this->params->dec = params_dec; }
}

template <class C, typename B, typename R, typename Q>
Launcher_uncoded<C,B,R,Q>
::~Launcher_uncoded()
{
}

template <class C, typename B, typename R, typename Q>
void Launcher_uncoded<C,B,R,Q>
::build_args()
{
	C::build_args();
}

template <class C, typename B, typename R, typename Q>
void Launcher_uncoded<C,B,R,Q>
::store_args()
{
	C::store_args();

	params_enc->type = "NO";
	params_enc->K    = this->params->src->K;
	params_enc->N_cw = params_enc->K;
	params_enc->R    = 1.f;

	params_dec->type   = "NONE";
	params_dec->implem = "HARD_DECISION";
	params_dec->K      = params_enc->K;
	params_dec->N_cw   = params_enc->N_cw;
	params_dec->R      = 1.f;

	this->params->pct->type = "NO";
	this->params->pct->K    = params_enc->K;
	this->params->pct->N    = this->params->pct->K;
	this->params->pct->N_cw = this->params->pct->N;
	this->params->pct->R    = 1.f;
}

template <class C, typename B, typename R, typename Q>
void Launcher_uncoded<C,B,R,Q>
::group_args()
{
	tools::Factory_encoder   ::group_args(this->arg_group);
	tools::Factory_decoder_NO::group_args(this->arg_group);

	C::group_args();
}

template <class C, typename B, typename R, typename Q>
void Launcher_uncoded<C,B,R,Q>
::print_header()
{
	if (params_enc->type != "NO")
		tools::Factory_encoder::header(this->pl_enc, *params_enc);
	tools::Factory_decoder_NO::header(this->pl_dec, *params_dec);

	C::print_header();
}

template <class C, typename B, typename R, typename Q>
void Launcher_uncoded<C,B,R,Q>
::build_codec()
{
	this->codec = new tools::Codec_uncoded<B,Q>(*params_enc, *params_dec);
}
}
}