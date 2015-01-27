#ifndef OCELOT_SITE_COMM_H
#define OCELOT_SITE_COMM_H
#include <string>
#include <boost/asio.hpp>
#include <queue>
#include <mutex>

#include "config.h"

using boost::asio::ip::tcp;

class site_comm {
	private:
		std::string site_host;
		std::string site_path;
		std::string site_password;
		std::mutex expire_queue_lock;
		std::string expire_token_buffer;
		std::queue<std::string> token_queue;
		bool readonly;
		bool t_active;
		void load_config(config * conf);
		void do_flush_tokens();

	public:
		bool verbose_flush;
		site_comm(config * conf);
		void reload_config(config * conf);
		bool all_clear();
		void expire_token(int torrent, int user);
		void flush_tokens();
		~site_comm();
};
#endif
