#include <set>
#include <thread>
#include <utility>
#include <sstream>
#include <fstream>
#include <cstring>
#include <exception>
#include <algorithm>

#include "Tools/Exception/exception.hpp"
#include "Module/Module.hpp"
#include "Module/Task.hpp"
#include "Module/Socket.hpp"
#include "Module/Loop/Loop.hpp"
#include "Module/Router/Router.hpp"
#include "Tools/Chain/Chain.hpp"

using namespace aff3ct;
using namespace aff3ct::tools;

#define TREE_ENGINE

Chain
::Chain(const module::Task &first, const module::Task &last, const size_t n_threads)
: n_threads(n_threads),
  sequences(n_threads, nullptr),
  tasks_sequences(n_threads),
  modules(n_threads),
  mtx_exception(new std::mutex()),
  force_exit_loop(new std::atomic<bool>(false))
{
	if (n_threads == 0)
	{
		std::stringstream message;
		message << "'n_threads' has to be strictly greater than 0.";
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, message.str());
	}

#ifndef TREE_ENGINE
	std::vector<std::vector<const module::Task*>> tasks_sequence;
	tasks_sequence.push_back(std::vector<const module::Task*>());
	std::vector<const module::Task*> loops;
	this->init_recursive(tasks_sequence, 0, this->subseq_types, loops, first, first, &last);
	if (tasks_sequence.back().back() != &last)
	{
		std::stringstream message;
		message << "'tasks_sequence.back().back()' has to be equal to '&last' ("
		        << "'tasks_sequence.back().back()'"             << " = " << +tasks_sequence.back().back()            << ", "
		        << "'&last'"                                    << " = " << +&last                                   << ", "
		        << "'tasks_sequence.back().back()->get_name()'" << " = " << tasks_sequence.back().back()->get_name() << ", "
		        << "'last.get_name()'"                          << " = " << last.get_name()                          << ").";
		throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
	}
	this->duplicate(tasks_sequence);
	for (auto &ts : this->tasks_sequences)
		for (size_t ss = 0; ss < ts.size(); ss++)
			if (this->subseq_types[ss] == subseq_t::LOOP)
			{
				auto loop = ts[ss].back();
				ts[ss].pop_back();
				ts[ss].insert(ts[ss].begin(), loop);
			}

	this->n_tasks = 0;
	this->task_id.resize(tasks_sequence.size());
	size_t id = 0;
	for (size_t ss = 0; ss < tasks_sequence.size(); ss++)
	{
		this->n_tasks += tasks_sequence[ss].size();
		this->task_id[ss].resize(tasks_sequence[ss].size());
		for (size_t ta = 0; ta < tasks_sequence[ss].size(); ta++)
			this->task_id[ss][ta] = id++;
	}
#else
	auto root = new Generic_node<Sub_sequence_const>(nullptr, {}, nullptr, 0, 0, 0);
	size_t ssid = 0;
	std::vector<const module::Task*> loops;
	this->init_recursive_new(root, ssid, loops, first, first, &last);
	this->duplicate_new(root);
	this->delete_tree(root);
#endif
}

Chain
::Chain(const module::Task &first, const size_t n_threads)
: n_threads(n_threads),
  sequences(n_threads, nullptr),
  tasks_sequences(n_threads),
  modules(n_threads),
  mtx_exception(new std::mutex()),
  force_exit_loop(new std::atomic<bool>(false))
{
	if (n_threads == 0)
	{
		std::stringstream message;
		message << "'n_threads' has to be strictly greater than 0.";
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, message.str());
	}

#ifndef TREE_ENGINE
	std::vector<std::vector<const module::Task*>> tasks_sequence;
	tasks_sequence.push_back(std::vector<const module::Task*>());
	std::vector<const module::Task*> loops;
	this->init_recursive(tasks_sequence, 0, this->subseq_types, loops, first, first);
	this->duplicate(tasks_sequence);
	for (auto &ts : this->tasks_sequences)
		for (size_t ss = 0; ss < ts.size(); ss++)
			if (this->subseq_types[ss] == subseq_t::LOOP)
			{
				auto loop = ts[ss].back();
				ts[ss].pop_back();
				ts[ss].insert(ts[ss].begin(), loop);
			}

	this->n_tasks = 0;
	this->task_id.resize(tasks_sequence.size());
	size_t id = 0;
	for (size_t ss = 0; ss < tasks_sequence.size(); ss++)
	{
		this->n_tasks += tasks_sequence[ss].size();
		this->task_id[ss].resize(tasks_sequence[ss].size());
		for (size_t ta = 0; ta < tasks_sequence[ss].size(); ta++)
			this->task_id[ss][ta] = id++;
	}
#else
	auto root = new Generic_node<Sub_sequence_const>(nullptr, {}, nullptr, 0, 0, 0);
	size_t ssid = 0;
	std::vector<const module::Task*> loops;
	this->init_recursive_new(root, ssid, loops, first, first);
	this->duplicate_new(root);
	this->delete_tree(root);
#endif
}

Chain::
~Chain()
{
#ifdef TREE_ENGINE
	for (auto s : this->sequences)
		this->delete_tree(s);
#endif
}

Chain* Chain
::clone() const
{
	auto c = new Chain(*this);
	std::vector<std::vector<const module::Task*>> tasks_sequence;
	for (size_t ss = 0; ss < this->tasks_sequences[0].size(); ss++)
	{
		tasks_sequence.push_back(std::vector<const module::Task*>());
		for (size_t t = 0; t < this->tasks_sequences[0][ss].size(); t++)
			tasks_sequence[ss].push_back(this->tasks_sequences[0][ss][t]);
	}
	c->duplicate(tasks_sequence);
	c->mtx_exception.reset(new std::mutex());
	c->force_exit_loop.reset(new std::atomic<bool>(false));
	return c;
}

void Chain
::export_dot_subsequence(const std::vector<module::Task*> &subseq,
                         const subseq_t &subseq_type,
                         const std::string &subseq_name,
                         const std::string &tab,
                               std::ostream &stream) const
{
	if (!subseq_name.empty())
	{
		stream << tab << "subgraph \"cluster_" << subseq_name << "\" {" << std::endl;
		stream << tab << tab << "node [style=filled];" << std::endl;
	}
	size_t exec_order = 0;
	for (auto &t : subseq)
	{
		stream << tab << tab << "subgraph \"cluster_" << +&t->get_module() << "_" << +&t << "\" {" << std::endl;
		stream << tab << tab << tab << "node [style=filled];" << std::endl;
		stream << tab << tab << tab << "subgraph \"cluster_" << +&t << "\" {" << std::endl;
		stream << tab << tab << tab << tab << "node [style=filled];" << std::endl;
		for (auto &s : t->sockets)
		{
			std::string stype;
			switch (t->get_socket_type(*s))
			{
				case module::socket_t::SIN: stype = "in"; break;
				case module::socket_t::SOUT: stype = "out"; break;
				case module::socket_t::SIN_SOUT: stype = "in_out"; break;
				default: stype = "unkn"; break;
			}
			stream << tab << tab << tab << tab << "\"" << +s.get() << "\""
			                                   << "[label=\"" << stype << ":" << s->get_name() << "\"];" << std::endl;
		}
		stream << tab << tab << tab << tab << "label=\"" << t->get_name() << "\";" << std::endl;
		stream << tab << tab << tab << tab << "color=blue;" << std::endl;
		stream << tab << tab << tab << "}" << std::endl;
		stream << tab << tab << tab << "label=\"" << t->get_module().get_name() << "\n"
		                            << "exec order: [" << exec_order++ << "]\n"
		                            << "addr: " << +&t->get_module() << "\";" << std::endl;
		stream << tab << tab << tab << "color=blue;" << std::endl;
		stream << tab << tab << "}" << std::endl;
	}
	if (!subseq_name.empty())
	{
		stream << tab << tab << "label=\"" << subseq_name << "\";" << std::endl;
		std::string color = subseq_type == subseq_t::LOOP ? "red" : "blue";
		stream << tab << tab << "color=" << color << ";" << std::endl;
		stream << tab << "}" << std::endl;
	}
}

void Chain
::export_dot(std::ostream &stream) const
{
#ifndef TREE_ENGINE
	std::string tab = "\t";
	stream << "digraph Chain {" << std::endl;

	for (size_t ss = 0; ss < this->tasks_sequences[0].size(); ss++)
	{
		std::string subseq_name = this->tasks_sequences[0].size() == 1 ? "" : "Sub-sequence"+std::to_string(ss);
		this->export_dot_subsequence(this->tasks_sequences[0][ss], this->subseq_types[ss], subseq_name, tab, stream);
	}

	for (size_t ss = 0; ss < this->tasks_sequences[0].size(); ss++)
	{
		auto &tasks_sequence = this->tasks_sequences[0][ss];
		for (auto &t : tasks_sequence)
		{
			for (auto &s : t->sockets)
			{
				if (t->get_socket_type(*s) == module::socket_t::SOUT ||
					t->get_socket_type(*s) == module::socket_t::SIN_SOUT)
				{
					for (auto &bs : s->get_bound_sockets())
					{
						stream << tab << "\"" << +s.get() << "\" -> \"" << +bs << "\"" << std::endl;
					}
				}
			}
		}
	}

	stream << "}" << std::endl;
#else
	this->export_dot_new(this->sequences[0], stream);
#endif
}

void Chain
::export_dot_new_subsequence(const std::vector<module::Task*> &subseq,
                             const subseq_t &subseq_type,
                             const std::string &subseq_name,
                             const std::string &tab,
                                   std::ostream &stream) const
{
	if (!subseq_name.empty())
	{
		stream << tab << "subgraph \"cluster_" << subseq_name << "\" {" << std::endl;
		stream << tab << tab << "node [style=filled];" << std::endl;
	}
	size_t exec_order = 0;
	for (auto &t : subseq)
	{
		stream << tab << tab << "subgraph \"cluster_" << +&t->get_module() << "_" << +&t << "\" {" << std::endl;
		stream << tab << tab << tab << "node [style=filled];" << std::endl;
		stream << tab << tab << tab << "subgraph \"cluster_" << +&t << "\" {" << std::endl;
		stream << tab << tab << tab << tab << "node [style=filled];" << std::endl;
		for (auto &s : t->sockets)
		{
			std::string stype;
			switch (t->get_socket_type(*s))
			{
				case module::socket_t::SIN: stype = "in"; break;
				case module::socket_t::SOUT: stype = "out"; break;
				case module::socket_t::SIN_SOUT: stype = "in_out"; break;
				default: stype = "unkn"; break;
			}
			stream << tab << tab << tab << tab << "\"" << +s.get() << "\""
			                                   << "[label=\"" << stype << ":" << s->get_name() << "\"];" << std::endl;
		}
		stream << tab << tab << tab << tab << "label=\"" << t->get_name() << "\";" << std::endl;
		stream << tab << tab << tab << tab << "color=blue;" << std::endl;
		stream << tab << tab << tab << "}" << std::endl;
		stream << tab << tab << tab << "label=\"" << t->get_module().get_name() << "\n"
		                            << "exec order: [" << exec_order++ << "]\n"
		                            << "addr: " << +&t->get_module() << "\";" << std::endl;
		stream << tab << tab << tab << "color=blue;" << std::endl;
		stream << tab << tab << "}" << std::endl;
	}
	if (!subseq_name.empty())
	{
		stream << tab << tab << "label=\"" << subseq_name << "\";" << std::endl;
		std::string color = subseq_type == subseq_t::LOOP ? "red" : "blue";
		stream << tab << tab << "color=" << color << ";" << std::endl;
		stream << tab << "}" << std::endl;
	}
}

void Chain
::export_dot_new_connections(const std::vector<module::Task*> &subseq,
                             const std::string &tab,
                                   std::ostream &stream) const
{
	for (auto &t : subseq)
	{
		for (auto &s : t->sockets)
		{
			if (t->get_socket_type(*s) == module::socket_t::SOUT ||
				t->get_socket_type(*s) == module::socket_t::SIN_SOUT)
			{
				for (auto &bs : s->get_bound_sockets())
				{
					stream << tab << "\"" << +s.get() << "\" -> \"" << +bs << "\"" << std::endl;
				}
			}
		}
	}
}

void Chain
::export_dot_new(Generic_node<Sub_sequence>* root, std::ostream &stream) const
{
	std::function<void(Generic_node<Sub_sequence>*,
	                   const std::string&,
	                   std::ostream&)> export_dot_new_subsequences_rec =
		[&, this](Generic_node<Sub_sequence>* cur_node, const std::string &tab, std::ostream &stream)
		{
			if (cur_node != nullptr)
			{
				this->export_dot_new_subsequence(cur_node->get_c()->tasks,
				                                 cur_node->get_c()->type,
				                                 "Sub-sequence"+std::to_string(cur_node->get_c()->id),
				                                 tab,
				                                 stream);

				for (auto c : cur_node->get_children())
					export_dot_new_subsequences_rec(c, tab, stream);
			}
		};

	std::function<void(Generic_node<Sub_sequence>*,
	                   const std::string&,
	                   std::ostream&)> export_dot_new_connections_rec =
		[&, this](Generic_node<Sub_sequence> *cur_node, const std::string &tab, std::ostream &stream)
		{
			if (cur_node != nullptr)
			{
				this->export_dot_new_connections(cur_node->get_c()->tasks, tab, stream);

				for (auto c : cur_node->get_children())
					export_dot_new_connections_rec(c, tab, stream);
			}
		};

	std::string tab = "\t";
	stream << "digraph Chain {" << std::endl;
	export_dot_new_subsequences_rec(root, tab, stream);
	export_dot_new_connections_rec (root, tab, stream);
	stream << "}" << std::endl;
}

std::vector<std::vector<const module::Module*>> Chain
::get_modules_per_threads() const
{
	std::vector<std::vector<const module::Module*>> modules_per_threads(modules.size());
	size_t tid = 0;
	for (auto &e : modules)
	{
		for (auto &ee : e)
			modules_per_threads[tid].push_back(ee.get());
		tid++;
	}
	return modules_per_threads;
}

std::vector<std::vector<const module::Module*>> Chain
::get_modules_per_types() const
{
	std::vector<std::vector<const module::Module*>> modules_per_types(modules[0].size());
	for (auto &e : modules)
	{
		size_t mid = 0;
		for (auto &ee : e)
			modules_per_types[mid++].push_back(ee.get());
	}
	return modules_per_types;
}

void Chain
::_exec(std::function<bool(const std::vector<int>&)> &stop_condition, std::vector<std::vector<module::Task*>> &tasks_sequence)
{
	try
	{
		std::vector<int> statuses(this->n_tasks, 0);
		do
		{
			for (size_t ss = 0; ss < tasks_sequence.size(); ss++)
				if (this->subseq_types[ss] == subseq_t::LOOP)
				{
					while (!(statuses[this->task_id[ss][0]] = tasks_sequence[ss][0]->exec()))
						for (size_t ta = 1; ta < tasks_sequence[ss].size(); ta++)
							statuses[this->task_id[ss][ta]] = tasks_sequence[ss][ta]->exec();
				}
				else
				{
					for (size_t ta = 0; ta < tasks_sequence[ss].size(); ta++)
						statuses[this->task_id[ss][ta]] = tasks_sequence[ss][ta]->exec();
				}
		}
		while (!*force_exit_loop && !stop_condition(statuses));
	}
	catch (std::exception const& e)
	{
		*force_exit_loop = true;

		this->mtx_exception->lock();

		auto save = tools::exception::no_backtrace;
		tools::exception::no_backtrace = true;
		std::string msg = e.what(); // get only the function signature
		tools::exception::no_backtrace = save;

		if (std::find(this->prev_exception_messages.begin(), this->prev_exception_messages.end(), msg) ==
			this->prev_exception_messages.end())
		{
			this->prev_exception_messages.push_back(msg); // save only the function signature
			this->prev_exception_messages_to_display.push_back(e.what()); // with backtrace if debug mode
		}
		else if (std::strlen(e.what()) > this->prev_exception_messages_to_display.back().size())
			this->prev_exception_messages_to_display[prev_exception_messages_to_display.size() -1] = e.what();

		this->mtx_exception->unlock();
	}
}

void Chain
::_exec_without_statuses(std::function<bool()> &stop_condition, std::vector<std::vector<module::Task*>> &tasks_sequence)
{
	try
	{
		do
		{
			for (size_t ss = 0; ss < tasks_sequence.size(); ss++)
				if (this->subseq_types[ss] == subseq_t::LOOP)
				{
					while (!tasks_sequence[ss][0]->exec())
						for (size_t ta = 1; ta < tasks_sequence[ss].size(); ta++)
							tasks_sequence[ss][ta]->exec();
				}
				else
				{
					for (size_t ta = 0; ta < tasks_sequence[ss].size(); ta++)
						tasks_sequence[ss][ta]->exec();
				}
		}
		while (!*force_exit_loop && !stop_condition());
	}
	catch (std::exception const& e)
	{
		*force_exit_loop = true;

		this->mtx_exception->lock();

		auto save = tools::exception::no_backtrace;
		tools::exception::no_backtrace = true;
		std::string msg = e.what(); // get only the function signature
		tools::exception::no_backtrace = save;

		if (std::find(this->prev_exception_messages.begin(), this->prev_exception_messages.end(), msg) ==
			this->prev_exception_messages.end())
		{
			this->prev_exception_messages.push_back(msg); // save only the function signature
			this->prev_exception_messages_to_display.push_back(e.what()); // with backtrace if debug mode
		}
		else if (std::strlen(e.what()) > this->prev_exception_messages_to_display.back().size())
			this->prev_exception_messages_to_display[prev_exception_messages_to_display.size() -1] = e.what();

		this->mtx_exception->unlock();
	}
}

void Chain
::_exec_without_statuses_new(std::function<bool()> &stop_condition, Generic_node<Sub_sequence>* sequence)
{
	std::function<void(Generic_node<Sub_sequence>*)> exec_sequence =
		[&exec_sequence](Generic_node<Sub_sequence>* cur_ss)
		{
			auto c = *cur_ss->get_c();
			if (c.type == subseq_t::LOOP)
			{
				while (!c.tasks[0]->exec())
					exec_sequence(cur_ss->get_children()[0]);
				static_cast<module::Loop&>(c.tasks[0]->get_module()).reset();
				exec_sequence(cur_ss->get_children()[1]);
			}
			else
			{
				for (size_t ta = 0; ta < c.tasks.size(); ta++)
					c.tasks[ta]->exec();
				for (auto c : cur_ss->get_children())
					exec_sequence(c);
			}
		};

	try
	{
		do
		{
			exec_sequence(sequence);
		}
		while (!*force_exit_loop && !stop_condition());
	}
	catch (std::exception const& e)
	{
		*force_exit_loop = true;

		this->mtx_exception->lock();

		auto save = tools::exception::no_backtrace;
		tools::exception::no_backtrace = true;
		std::string msg = e.what(); // get only the function signature
		tools::exception::no_backtrace = save;

		if (std::find(this->prev_exception_messages.begin(), this->prev_exception_messages.end(), msg) ==
			this->prev_exception_messages.end())
		{
			this->prev_exception_messages.push_back(msg); // save only the function signature
			this->prev_exception_messages_to_display.push_back(e.what()); // with backtrace if debug mode
		}
		else if (std::strlen(e.what()) > this->prev_exception_messages_to_display.back().size())
			this->prev_exception_messages_to_display[prev_exception_messages_to_display.size() -1] = e.what();

		this->mtx_exception->unlock();
	}
}

void Chain
::exec(std::function<bool(const std::vector<int>&)> stop_condition)
{
	std::vector<std::thread> threads(n_threads);
	for (size_t tid = 1; tid < n_threads; tid++)
		threads[tid] = std::thread(&Chain::_exec, this, std::ref(stop_condition), std::ref(this->tasks_sequences[tid]));

	this->_exec(stop_condition, this->tasks_sequences[0]);

	for (size_t tid = 1; tid < n_threads; tid++)
		threads[tid].join();

	if (!this->prev_exception_messages_to_display.empty())
	{
		*force_exit_loop = false;
		throw std::runtime_error(this->prev_exception_messages_to_display.back());
	}
}

#ifndef TREE_ENGINE
void Chain
::exec(std::function<bool()> stop_condition)
{
	std::vector<std::thread> threads(n_threads);
	for (size_t tid = 1; tid < n_threads; tid++)
		threads[tid] = std::thread(&Chain::_exec_without_statuses, this, std::ref(stop_condition),
		                           std::ref(this->tasks_sequences[tid]));

	this->_exec_without_statuses(stop_condition, this->tasks_sequences[0]);

	for (size_t tid = 1; tid < n_threads; tid++)
		threads[tid].join();

	if (!this->prev_exception_messages_to_display.empty())
	{
		*force_exit_loop = false;
		throw std::runtime_error(this->prev_exception_messages_to_display.back());
	}
}
#else
void Chain
::exec(std::function<bool()> stop_condition)
{
	std::vector<std::thread> threads(n_threads);
	for (size_t tid = 1; tid < n_threads; tid++)
		threads[tid] = std::thread(&Chain::_exec_without_statuses_new, this, std::ref(stop_condition),
		                           std::ref(this->sequences[tid]));

	this->_exec_without_statuses_new(stop_condition, this->sequences[0]);

	for (size_t tid = 1; tid < n_threads; tid++)
		threads[tid].join();

	if (!this->prev_exception_messages_to_display.empty())
	{
		*force_exit_loop = false;
		throw std::runtime_error(this->prev_exception_messages_to_display.back());
	}
}
#endif

int Chain
::exec(const size_t tid)
{
	if (tid >= this->tasks_sequences.size())
	{
		std::stringstream message;
		message << "'tid' has to be smaller than 'tasks_sequences.size()' ('tid' = " << tid
		        << ", 'tasks_sequences.size()' = " << this->tasks_sequences.size() << ").";
		throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
	}

	int ret = 0;
	for (size_t ss = 0; ss < this->tasks_sequences[tid].size(); ss++)
	{
		if (this->subseq_types[ss] == subseq_t::LOOP)
		{
			while (!this->tasks_sequences[tid][ss][0]->exec())
				for (size_t ta = 1; ta < this->tasks_sequences[tid][ss].size(); ta++)
					ret += this->tasks_sequences[tid][ss][ta]->exec();
			ret++;
		}
		else
		{
			for (size_t ta = 0; ta < this->tasks_sequences[tid][ss].size(); ta++)
				ret += this->tasks_sequences[tid][ss][ta]->exec();
		}
	}
	return ret;
}

void Chain
::init_recursive(std::vector<std::vector<const module::Task*>> &tasks_sequence,
                 const size_t ssid,
                 std::vector<subseq_t> &subseq_types,
                 std::vector<const module::Task*> &loops,
                 const module::Task& first,
                 const module::Task& current_task,
                 const module::Task *last)
{
	if (auto loop = dynamic_cast<const module::Loop*>(&current_task.get_module()))
	{
		if (std::find(loops.begin(), loops.end(), &current_task) == loops.end())
		{
			loops.push_back(&current_task);
			if (&current_task != &first)
				tasks_sequence.push_back(std::vector<const module::Task*>());
			tasks_sequence.push_back(std::vector<const module::Task*>());

			const auto ssid1 = tasks_sequence.size() -2;
			const auto ssid2 = tasks_sequence.size() -1;

			if (loop->tasks[0]->sockets[2]->get_bound_sockets().size() == 1)
			{
				subseq_types.push_back(subseq_t::LOOP);
				auto &t1 = loop->tasks[0]->sockets[2]->get_bound_sockets()[0]->get_task();
				Chain::init_recursive(tasks_sequence, ssid1, subseq_types, loops, first, t1, last);
				tasks_sequence[ssid1].push_back(&current_task);
			}
			else
			{
				std::stringstream message;
				message << "'loop->tasks[0]->sockets[2]->get_bound_sockets().size()' has to be equal to 1 ("
				        << "'loop->tasks[0]->sockets[2]->get_bound_sockets().size()' = "
				        << loop->tasks[0]->sockets[2]->get_bound_sockets().size() << ").";
				throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
			}

			if (loop->tasks[0]->sockets[3]->get_bound_sockets().size() == 1)
			{
				auto &t2 = loop->tasks[0]->sockets[3]->get_bound_sockets()[0]->get_task();
				Chain::init_recursive(tasks_sequence, ssid2, subseq_types, loops, first, t2, last);
			}
			else
			{
				std::stringstream message;
				message << "'loop->tasks[0]->sockets[3]->get_bound_sockets().size()' has to be equal to 1 ("
				        << "'loop->tasks[0]->sockets[3]->get_bound_sockets().size()' = "
				        << loop->tasks[0]->sockets[3]->get_bound_sockets().size() << ").";
				throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
			}
		}
	}
	else
	{
		if (subseq_types.size() < tasks_sequence.size())
			subseq_types.push_back(subseq_t::STD);

		tasks_sequence[ssid].push_back(&current_task);

		if (&current_task != last)
		{
			for (auto &s : current_task.sockets)
			{
				if (current_task.get_socket_type(*s) == module::socket_t::SIN_SOUT ||
					current_task.get_socket_type(*s) == module::socket_t::SOUT)
				{
					auto bss = s->get_bound_sockets();
					for (auto &bs : bss)
					{
						if (bs != nullptr)
						{
							auto &t = bs->get_task();
							if (t.is_last_input_socket(*bs) || dynamic_cast<const module::Loop*>(&t.get_module()))
								Chain::init_recursive(tasks_sequence, ssid, subseq_types, loops, first, t, last);
						}
					}
				}
				else if (current_task.get_socket_type(*s) == module::socket_t::SIN)
				{
					if (s->get_bound_sockets().size() > 1)
					{
						std::stringstream message;
						message << "'s->get_bound_sockets().size()' has to be smaller or equal to 1 ("
						        << "'s->get_bound_sockets().size()'"         << " = " << s->get_bound_sockets().size() << ", "
						        << "'get_socket_type(*s)'"                   << " = " << "socket_t::SIN"               << ", "
						        << "'s->get_name()'"                         << " = " << s->get_name()                 << ", "
						        << "'s->get_task().get_name()'"              << " = " << s->get_task().get_name()      << ", "
						        << "'s->get_task().get_module().get_name()'" << " = " << s->get_task().get_module().get_name()
						        << ").";
						throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
					}
				}
			}
		}
	}
}

void Chain
::init_recursive_new(Generic_node<Sub_sequence_const> *cur_subseq,
                     size_t &ssid,
                     std::vector<const module::Task*> &loops,
                     const module::Task &first,
                     const module::Task &current_task,
                     const module::Task *last)
{
	if (auto loop = dynamic_cast<const module::Loop*>(&current_task.get_module()))
	{
		if (std::find(loops.begin(), loops.end(), &current_task) == loops.end())
		{
			loops.push_back(&current_task);
			Generic_node<Sub_sequence_const>* node_loop = nullptr;
			if (&first == &current_task)
				node_loop = cur_subseq;
			else
			{
				ssid++;
				node_loop = new Generic_node<Sub_sequence_const>(cur_subseq, {}, nullptr, cur_subseq->get_depth() +1, 0, 0);
			}

			auto node_loop_son0 = new Generic_node<Sub_sequence_const>(node_loop, {}, nullptr, node_loop->get_depth() +1, 0, 0);
			auto node_loop_son1 = new Generic_node<Sub_sequence_const>(node_loop, {}, nullptr, node_loop->get_depth() +1, 0, 1);
			node_loop->add_child(node_loop_son0);
			node_loop->add_child(node_loop_son1);

			node_loop->set_contents(new Sub_sequence_const());
			node_loop_son0->set_contents(new Sub_sequence_const());
			node_loop_son1->set_contents(new Sub_sequence_const());

			node_loop->get_c()->tasks.push_back(&current_task);
			node_loop->get_c()->type = subseq_t::LOOP;
			node_loop->get_c()->id = ssid++;

			if (!cur_subseq->get_children().size())
				cur_subseq->add_child(node_loop);

			if (loop->tasks[0]->sockets[2]->get_bound_sockets().size() == 1)
			{
				node_loop_son0->get_c()->id = ssid++;
				auto &t = loop->tasks[0]->sockets[2]->get_bound_sockets()[0]->get_task();
				Chain::init_recursive_new(node_loop_son0, ssid, loops, first, t, last);
			}
			else
			{
				std::stringstream message;
				message << "'loop->tasks[0]->sockets[2]->get_bound_sockets().size()' has to be equal to 1 ("
				        << "'loop->tasks[0]->sockets[2]->get_bound_sockets().size()' = "
				        << loop->tasks[0]->sockets[2]->get_bound_sockets().size() << ").";
				throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
			}

			if (loop->tasks[0]->sockets[3]->get_bound_sockets().size() == 1)
			{
				node_loop_son1->get_c()->id = ssid++;
				auto &t = loop->tasks[0]->sockets[3]->get_bound_sockets()[0]->get_task();
				Chain::init_recursive_new(node_loop_son1, ssid, loops, first, t, last);
			}
			else
			{
				std::stringstream message;
				message << "'loop->tasks[0]->sockets[3]->get_bound_sockets().size()' has to be equal to 1 ("
				        << "'loop->tasks[0]->sockets[3]->get_bound_sockets().size()' = "
				        << loop->tasks[0]->sockets[3]->get_bound_sockets().size() << ").";
				throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
			}
		}
	}
	else
	{
		if (&first == &current_task)
			cur_subseq->set_contents(new Sub_sequence_const());

		cur_subseq->get_c()->tasks.push_back(&current_task);

		if (&current_task != last)
		{
			for (auto &s : current_task.sockets)
			{
				if (current_task.get_socket_type(*s) == module::socket_t::SIN_SOUT ||
					current_task.get_socket_type(*s) == module::socket_t::SOUT)
				{
					auto bss = s->get_bound_sockets();
					for (auto &bs : bss)
					{
						if (bs != nullptr)
						{
							auto &t = bs->get_task();
							if (t.is_last_input_socket(*bs) || dynamic_cast<const module::Loop*>(&t.get_module()))
								Chain::init_recursive_new(cur_subseq, ssid, loops, first, t, last);
						}
					}
				}
				else if (current_task.get_socket_type(*s) == module::socket_t::SIN)
				{
					if (s->get_bound_sockets().size() > 1)
					{
						std::stringstream message;
						message << "'s->get_bound_sockets().size()' has to be smaller or equal to 1 ("
						        << "'s->get_bound_sockets().size()'"         << " = " << s->get_bound_sockets().size() << ", "
						        << "'get_socket_type(*s)'"                   << " = " << "socket_t::SIN"               << ", "
						        << "'s->get_name()'"                         << " = " << s->get_name()                 << ", "
						        << "'s->get_task().get_name()'"              << " = " << s->get_task().get_name()      << ", "
						        << "'s->get_task().get_module().get_name()'" << " = " << s->get_task().get_module().get_name()
						        << ").";
						throw tools::runtime_error(__FILE__, __LINE__, __func__, message.str());
					}
				}
			}
		}
	}
}

void Chain
::duplicate(const std::vector<std::vector<const module::Task*>> &tasks_sequence)
{
	std::vector<std::pair<const module::Module*, std::vector<const module::Task*>>> modules_to_tasks;
	std::map<const module::Task*, size_t> task_to_module_id;

	auto exist_module = [](const std::vector<std::pair<const module::Module*, std::vector<const module::Task*>>>
	                       &modules_to_tasks,
	                       const module::Module *m) -> int
	{
		for (int i = 0; i < (int)modules_to_tasks.size(); i++)
			if (modules_to_tasks[i].first == m)
				return i;
		return -1;
	};

	for (auto &subsec : tasks_sequence)
	{
		for (auto &ta : subsec)
		{
			auto m = &ta->get_module();
			auto id = exist_module(modules_to_tasks, m);
			if (id == -1)
			{
				std::vector<const module::Task*> vt = {ta};
				task_to_module_id[ta] = modules_to_tasks.size();
				modules_to_tasks.push_back(std::make_pair(m, vt));
			}
			else
			{
				modules_to_tasks[id].second.push_back(ta);
				task_to_module_id[ta] = id;
			}
		}
	}

	// clone the modules
	for (size_t tid = 0; tid < this->n_threads; tid++)
	{
		this->modules[tid].resize(modules_to_tasks.size());
		size_t m = 0;
		for (auto &mtt : modules_to_tasks)
			this->modules[tid][m++].reset(mtt.first->clone());
	}

	auto get_task_id = [&tasks_sequence](const module::Task &ta) -> std::pair<int,int>
	{
		for (size_t ss = 0; ss < tasks_sequence.size(); ss++)
			for (size_t t = 0; t < tasks_sequence[ss].size(); t++)
				if (&ta == tasks_sequence[ss][t])
					return std::make_pair(ss, t);
		return std::make_pair(-1, -1);
	};

	auto get_socket_id = [](const module::Task &ta, const module::Socket& so) -> int
	{
		for (size_t s = 0; s < ta.sockets.size(); s++)
			if (&so == ta.sockets[s].get())
				return (int)s;
		return -1;
	};

	auto get_task_id_in_module = [](const module::Task* t) -> int
	{
		auto &m = t->get_module();

		for (size_t tt = 0; tt < m.tasks.size(); tt++)
			if (m.tasks[tt].get() == t)
				return tt;
		return -1;
	};

	auto get_task_cpy = [this, &task_to_module_id, &get_task_id_in_module, &tasks_sequence](const size_t tid,
		const size_t ssid, const size_t taid) -> module::Task&
	{
		const auto module_id = task_to_module_id[tasks_sequence[ssid][taid]];
		const auto task_id_in_module = get_task_id_in_module(tasks_sequence[ssid][taid]);
		assert(task_id_in_module != -1);
		return *modules[tid][module_id]->tasks[task_id_in_module].get();
	};

	// create the n tasks sequences
	for (size_t tid = 0; tid < this->n_threads; tid++)
	{
		auto &tasks_ss_chain_cpy = this->tasks_sequences[tid];
		tasks_ss_chain_cpy.clear();
		for (size_t ss = 0; ss < tasks_sequence.size(); ss++)
		{
			tasks_ss_chain_cpy.push_back(std::vector<module::Task*>());
			for (size_t taid = 0; taid < tasks_sequence[ss].size(); taid++)
			{
				auto &task_cpy = get_task_cpy(tid, ss, taid);
				task_cpy.set_autoalloc(true);
				tasks_ss_chain_cpy[ss].push_back(&task_cpy);
			}
		}
	}

	// bind the tasks of the n sequences
	for (size_t ssout_id = 0; ssout_id < tasks_sequence.size(); ssout_id++)
	{
		for (size_t tout_id = 0; tout_id < tasks_sequence[ssout_id].size(); tout_id++)
		{
			auto &tout = tasks_sequence[ssout_id][tout_id];
			for (size_t sout_id = 0; sout_id < tout->sockets.size(); sout_id++)
			{
				auto &sout = tout->sockets[sout_id];
				if (tout->get_socket_type(*sout) == module::socket_t::SIN_SOUT ||
					tout->get_socket_type(*sout) == module::socket_t::SOUT)
				{
					for (auto &sin : sout->get_bound_sockets())
					{
						if (sin != nullptr)
						{
							auto &tin = sin->get_task();
							auto tin_id_pair = get_task_id(tin);

							auto ssin_id = tin_id_pair.first;
							auto tin_id = tin_id_pair.second;

							if (tin_id != -1 && ssin_id != -1)
							{
								auto sin_id = get_socket_id(tin, *sin);
								assert(sin_id != -1);

								for (size_t tid = 0; tid < this->n_threads; tid++)
								{
									auto &tasks_chain_cpy = this->tasks_sequences[tid];
									(*tasks_chain_cpy[ssin_id][tin_id])[sin_id].bind((*tasks_chain_cpy[ssout_id][tout_id])[sout_id]);
								}
							}
						}
					}
				}
			}
		}
	}
}

void Chain
::duplicate_new(const Generic_node<Sub_sequence_const> *sequence)
{
	std::set<const module::Module*> modules_set;

	std::function<void(const Generic_node<Sub_sequence_const>*)> collect_modules_list;
	collect_modules_list = [&](const Generic_node<Sub_sequence_const> *node)
	{
		if (node != nullptr)
		{
			if (node->get_c())
				for (auto ta : node->get_c()->tasks)
					modules_set.insert(&ta->get_module());
			for (auto c : node->get_children())
				collect_modules_list(c);
		}
	};
	collect_modules_list(sequence);

	std::vector<const module::Module*> modules_vec;
	for (auto m : modules_set)
		modules_vec.push_back(m);

	// clone the modules
	for (size_t tid = 0; tid < this->n_threads; tid++)
	{
		this->modules[tid].resize(modules_vec.size());
		for (size_t m = 0; m < modules_vec.size(); m++)
			this->modules[tid][m].reset(modules_vec[m]->clone());
	}

	auto get_module_id = [](const std::vector<const module::Module*> &modules, const module::Module &module)
	{
		int m_id;
		for (m_id = 0; m_id < (int)modules.size(); m_id++)
			if (modules[m_id] == &module)
				return m_id;
		return -1;
	};

	auto get_task_id = [](const std::vector<std::shared_ptr<module::Task>> &tasks, const module::Task &task)
	{
		int t_id;
		for (t_id = 0; t_id < (int)tasks.size(); t_id++)
			if (tasks[t_id].get() == &task)
				return t_id;
		return -1;
	};

	auto get_socket_id = [](const std::vector<std::shared_ptr<module::Socket>> &sockets, const module::Socket &socket)
	{
		int s_id;
		for (s_id = 0; s_id < (int)sockets.size(); s_id++)
			if (sockets[s_id].get() == &socket)
				return s_id;
		return -1;
	};

	std::function<void(const Generic_node<Sub_sequence_const>*,
	                         Generic_node<Sub_sequence>*,
	                   const size_t)> duplicate_sequence;

	duplicate_sequence = [&](const Generic_node<Sub_sequence_const> *sequence_ref,
	                               Generic_node<Sub_sequence      > *sequence_cpy,
	                         const size_t thread_id)
	{
		if (sequence_ref != nullptr && sequence_ref->get_c())
		{
			auto ss_ref = sequence_ref->get_c();
			auto ss_cpy = new Sub_sequence();

			ss_cpy->type = ss_ref->type;
			ss_cpy->id = ss_ref->id;
			for (auto t_ref : ss_ref->tasks)
			{
				auto &m_ref = t_ref->get_module();

				auto m_id = get_module_id(modules_vec, m_ref);
				auto t_id = get_task_id(m_ref.tasks, *t_ref);

				assert(m_id != -1);
				assert(t_id != -1);

				// add the task to the sub-sequence
				ss_cpy->tasks.push_back(this->modules[thread_id][m_id]->tasks[t_id].get());

				// replicate the sockets binding
				for (size_t s_id = 0; s_id < t_ref->sockets.size(); s_id++)
				{
					if (t_ref->get_socket_type(*t_ref->sockets[s_id]) == module::socket_t::SIN_SOUT ||
					    t_ref->get_socket_type(*t_ref->sockets[s_id]) == module::socket_t::SIN)
					{
						try
						{
							auto &s_ref_out = t_ref->sockets[s_id]->get_bound_socket(); // can raise an exception
							auto &t_ref_out = s_ref_out.get_task();
							auto &m_ref_out = t_ref_out.get_module();

							auto m_id_out = get_module_id(modules_vec, m_ref_out);
							auto t_id_out = get_task_id(m_ref_out.tasks, t_ref_out);
							auto s_id_out = get_socket_id(t_ref_out.sockets, s_ref_out);

							assert(m_id_out != -1);
							assert(t_id_out != -1);
							assert(s_id_out != -1);

							auto &s_in = (*this->modules[thread_id][m_id])[t_id][s_id];
							auto &s_out = (*this->modules[thread_id][m_id_out])[t_id_out][s_id_out];

							s_in.bind(s_out);
						}
						catch (...) {}
					}
				}
			}

			sequence_cpy->set_contents(ss_cpy);

			for (size_t c = 0; c < sequence_ref->get_children().size(); c++)
				sequence_cpy->add_child(new Generic_node<Sub_sequence>(sequence_cpy,
				                                                       {},
				                                                       nullptr,
				                                                       sequence_cpy->get_depth() +1,
				                                                       0,
				                                                       c));

			for (size_t c = 0; c < sequence_ref->get_children().size(); c++)
				duplicate_sequence(sequence_ref->get_children()[c], sequence_cpy->get_children()[c], thread_id);
		}
	};

	for (size_t thread_id = 0; thread_id < this->sequences.size(); thread_id++)
	{
		this->sequences[thread_id] = new Generic_node<Sub_sequence>(nullptr, {}, nullptr, 0, 0, 0);
		duplicate_sequence(sequence, this->sequences[thread_id], thread_id);
	}
}