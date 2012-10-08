#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <fstream>
#include <boost/thread/mutex.hpp>
#include "site_comm.h"
#include "ocelot.h"

enum tracker_status { OPEN, PAUSED, CLOSING }; // tracker status

class worker {
	private:
		torrent_list torrents_list;
		user_list users_list;
		std::vector<std::string> whitelist;
		std::unordered_map<std::string, del_message> del_reasons;
		config * conf;
		mysql * db;
		void do_reap_peers();
		void do_reap_del_reasons();
		tracker_status status;
		site_comm s_comm;
		std::string get_del_reason(int code);
		boost::mutex del_reasons_lock;
		bool user_is_visible(user *u);

	public:
		worker(torrent_list &torrents, user_list &users, std::vector<std::string> &_whitelist, config * conf_obj, mysql * db_obj, site_comm &sc);
		std::string work(std::string &input, std::string &ip, bool &gzip);
		std::string error(std::string err);
		std::string announce(torrent &tor, user &u, std::map<std::string, std::string> &params, std::map<std::string, std::string> &headers, std::string &ip, bool &gzip);
		std::string scrape(const std::list<std::string> &infohashes, std::map<std::string, std::string> &headers, bool &gzip);
		std::string update(std::map<std::string, std::string> &params);

		bool signal(int sig);

		tracker_status get_status() { return status; }

		void reap_peers();
};
