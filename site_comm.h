#ifndef OCELOT_SITE_COMM_H
#define OCELOT_SITE_COMM_H
#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <boost/asio.hpp>

#include "config.h"

using boost::asio::ip::tcp;

class site_comm {
	private:
		config conf;
	
	public:
		site_comm(config &conf);
		bool expire_token(int torrent, int user);
		~site_comm();
};
#endif
