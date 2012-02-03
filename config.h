#ifndef OCELOT_CONFIG_H
#define OCELOT_CONFIG_H

#include <string>

class config {
	public:
		std::string host;
		std::string site_host;
		unsigned int port;
		unsigned int max_connections;
		unsigned int max_read_buffer;
		unsigned int timeout_interval;
		unsigned int schedule_interval;
		unsigned int max_middlemen;
		
		unsigned int announce_interval;
		int peers_timeout;
		
		unsigned int reap_peers_interval;
		
		// MySQL
		std::string mysql_db;
		std::string mysql_host;
		std::string mysql_username;
		std::string mysql_password;
		
		std::string site_password;
		
		config();
};

#endif
