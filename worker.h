#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <fstream>
#include "site_comm.h"

enum tracker_status { OPEN, PAUSED, CLOSING }; // tracker status

class worker {
	private:
		torrent_list torrents_list;
		user_list users_list;
		std::vector<std::string> whitelist;
		config * conf;
		mysql * db;
		void do_reap_peers();
		tracker_status status;
		site_comm s_comm;

	public:
		worker(torrent_list &torrents, user_list &users, std::vector<std::string> &_whitelist, config * conf_obj, mysql * db_obj, site_comm &sc);
		std::string work(std::string &input, std::string &ip);
		std::string error(std::string err);
		std::string announce(torrent &tor, user &u, std::map<std::string, std::string> &params, std::map<std::string, std::string> &headers, std::string &ip);
		std::string scrape(const std::list<std::string> &infohashes);
		std::string update(std::map<std::string, std::string> &params);

		bool signal(int sig);

		tracker_status get_status() { return status; }

		void reap_peers();
};
