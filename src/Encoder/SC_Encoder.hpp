#ifndef SC_ENCODER_HPP_
#define SC_ENCODER_HPP_

#ifdef SYSTEMC
#include <vector>
#include <string>
#include <cassert>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>

#include "../Tools/MIPP/mipp.h"

template <typename B>
class SC_Encoder;

template <typename B>
class SC_Encoder_sockets : public sc_core::sc_module
{
	SC_HAS_PROCESS(SC_Encoder_sockets);

public:
	tlm_utils::simple_target_socket   <SC_Encoder_sockets> in;
	tlm_utils::simple_initiator_socket<SC_Encoder_sockets> out;

private:
	SC_Encoder<B> &encoder;
	mipp::vector<B> U_K;
	mipp::vector<B> X_N;

public:
	SC_Encoder_sockets(SC_Encoder<B> &encoder, const sc_core::sc_module_name name = "SC_Encoder_sockets")
	: sc_module(name), in("in"), out("out"),
	  encoder(encoder),
	  U_K(encoder.K * encoder.n_frames),
	  X_N(encoder.N * encoder.n_frames)
	{
		in.register_b_transport(this, &SC_Encoder_sockets::b_transport);
	}

	void resize_buffers()
	{
		if ((int)U_K.size() != encoder.K * encoder.n_frames) U_K.resize(encoder.K * encoder.n_frames);
		if ((int)X_N.size() != encoder.N * encoder.n_frames) X_N.resize(encoder.N * encoder.n_frames);
	}

private:
	void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& t)
	{
		assert((trans.get_data_length() / sizeof(B)) == (int)U_K.size());

		const B* buffer_in = (B*)trans.get_data_ptr();
		std::copy(buffer_in, buffer_in + U_K.size(), U_K.begin());

		encoder.encode(U_K, X_N);

		tlm::tlm_generic_payload payload;
		payload.set_data_ptr((unsigned char*)X_N.data());
		payload.set_data_length(X_N.size() * sizeof(B));

		sc_core::sc_time zero_time(sc_core::SC_ZERO_TIME);
		out->b_transport(payload, zero_time);
	}
};

template <typename B>
class SC_Encoder : public Encoder_interface<B>
{
	friend SC_Encoder_sockets<B>;

private:
	std::string name;

public:
	SC_Encoder_sockets<B> *sockets;

public:
	SC_Encoder(const int K, const int N, const int n_frames = 1, const std::string name = "SC_Encoder")
	: Encoder_interface<B>(K, N, n_frames, name), name(name), sockets(nullptr) {}

	virtual ~SC_Encoder() { if (sockets != nullptr) { delete sockets; sockets = nullptr; } }

	virtual void encode(const mipp::vector<B>& U_K, mipp::vector<B>& X_N) = 0;

	virtual void set_n_frames(const int n_frames)
	{
		Encoder_interface<B>::set_n_frames(n_frames);

		if (sockets != nullptr)
			sockets->resize_buffers();
	}

	void create_sc_sockets()
	{
		this->sockets = new SC_Encoder_sockets<B>(*this, name.c_str());
	}
};

template <typename B>
using Encoder = SC_Encoder<B>;
#else
template <typename B>
using Encoder = Encoder_interface<B>;
#endif

#endif /* SC_ENCODER_HPP_ */