#ifndef OCELOT_DB_H
#define OCELOT_DB_H
#pragma GCC visibility push(default)
#include <mysql++/mysql++.h>
#include <string>
#include <unordered_map>
#include <queue>
#include <boost/thread/mutex.hpp>

#include "logger.h"

class mysql {
	private:
		mysqlpp::Connection conn;
		std::string update_user_buffer;
		std::string update_torrent_buffer;
		std::string update_peer_buffer;
		std::string update_snatch_buffer;
		std::string update_token_buffer;
		
		std::queue<std::string> user_queue;
		std::queue<std::string> torrent_queue;
		std::queue<std::string> peer_queue;
		std::queue<std::string> snatch_queue;
		std::queue<std::string> token_queue;

		std::string db, server, db_user, pw;
		bool u_active, t_active, p_active, s_active, tok_active;

		// These locks prevent more than one thread from reading/writing the buffers.
		// These should be held for the minimum time possible.
		boost::mutex user_buffer_lock;
		boost::mutex torrent_buffer_lock;
		boost::mutex peer_buffer_lock;
		boost::mutex snatch_buffer_lock;
		boost::mutex user_token_lock;
		
		void do_flush_users();
		void do_flush_torrents();
		void do_flush_snatches();
		void do_flush_peers();
		void do_flush_tokens();

		void flush_users();
		void flush_torrents();
		void flush_snatches();
		void flush_peers();
		void flush_tokens();
		void clear_peer_data();

	public:
		mysql(std::string mysql_db, std::string mysql_host, std::string username, std::string password);
		void load_torrents(std::unordered_map<std::string, torrent> &torrents);
		void load_users(std::unordered_map<std::string, user> &users);
		void load_tokens(std::unordered_map<std::string, torrent> &torrents);
		void load_whitelist(std::vector<std::string> &whitelist);
		
		void record_user(std::string &record); // (id,uploaded_change,downloaded_change)
		void record_torrent(std::string &record); // (id,seeders,leechers,snatched_change,balance)
		void record_snatch(std::string &record); // (uid,fid,tstamp)
		void record_peer(std::string &record, std::string &ip, std::string &peer_id, std::string &useragent); // (uid,fid,active,peerid,useragent,ip,uploaded,downloaded,upspeed,downspeed,left,timespent,announces)
		void record_token(std::string &record);

		void flush();

		bool all_clear();
		
		boost::mutex torrent_list_mutex;

		logger* logger_ptr;
};

#pragma GCC visibility pop
#endif
