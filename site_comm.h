#ifndef OCELOT_SITE_COMM_H
#define OCELOT_SITE_COMM_H
#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <boost/asio.hpp>
#include <queue>
#include <boost/thread/mutex.hpp>

#include "config.h"

using boost::asio::ip::tcp;

class site_comm {
	private:
		config conf;
		boost::mutex expire_queue_lock;
		std::string expire_token_buffer;
		std::queue<std::string> token_queue;
		bool t_active;

	public:
		bool verbose_flush;
		site_comm(config &conf);
		bool all_clear();
		void expire_token(int torrent, int user);
		void flush_tokens();
		void do_flush_tokens();
		~site_comm();
};
#endif
