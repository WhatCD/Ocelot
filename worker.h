#ifndef WORKER_H
#define WORKER_H

#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <iostream>
#include <mutex>
#include <ctime>
#include "site_comm.h"
#include "ocelot.h"

enum tracker_status { OPEN, PAUSED, CLOSING }; // tracker status

class worker {
	private:
		config * conf;
		mysql * db;
		site_comm * s_comm;
		torrent_list &torrents_list;
		user_list &users_list;
		std::vector<std::string> &whitelist;
		std::unordered_map<std::string, del_message> del_reasons;
		tracker_status status;
		bool reaper_active;
		time_t cur_time;

		unsigned int announce_interval;
		unsigned int del_reason_lifetime;
		unsigned int peers_timeout;
		unsigned int numwant_limit;
		bool keepalive_enabled;
		std::string site_password;
		std::string report_password;

		std::mutex del_reasons_lock;
		void load_config(config * conf);
		void do_start_reaper();
		void reap_peers();
		void reap_del_reasons();
		std::string get_del_reason(int code);
		peer_list::iterator add_peer(peer_list &peer_list, const std::string &peer_id);
		inline bool peer_is_visible(user_ptr &u, peer *p);

	public:
		worker(config * conf_obj, torrent_list &torrents, user_list &users, std::vector<std::string> &_whitelist, mysql * db_obj, site_comm * sc);
		void reload_config(config * conf);
		std::string work(const std::string &input, std::string &ip, client_opts_t &client_opts);
		std::string announce(const std::string &input, torrent &tor, user_ptr &u, params_type &params, params_type &headers, std::string &ip, client_opts_t &client_opts);
		std::string scrape(const std::list<std::string> &infohashes, params_type &headers, client_opts_t &client_opts);
		std::string update(params_type &params, client_opts_t &client_opts);

		void reload_lists();
		bool shutdown();

		tracker_status get_status() { return status; }

		void start_reaper();
};
#endif
