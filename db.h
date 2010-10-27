#ifndef OCELOT_DB_H
#define OCELOT_DB_H
#pragma GCC visibility push(default)
#include <mysql++/mysql++.h>
#include <string>
#include <unordered_map>
#include <boost/thread/mutex.hpp>

class mysql {
	private:
		mysqlpp::Connection conn;
		std::string update_user_buffer;
		std::string update_torrent_buffer;
		std::string update_peer_buffer;
		std::string update_snatch_buffer;
		
		boost::mutex user_buffer_lock;
		boost::mutex torrent_buffer_lock;
		boost::mutex peer_buffer_lock;
		boost::mutex snatch_buffer_lock;
		
		boost::mutex user_list_mutex;
		
		boost::mutex db_mutex;
	public:
		mysql(std::string mysql_db, std::string mysql_host, std::string username, std::string password);
		void load_torrents(std::unordered_map<std::string, torrent> &torrents);
		void load_users(std::unordered_map<std::string, user> &users);
		void load_whitelist(std::vector<std::string> &whitelist);
		
		void record_user(std::string &record); // (id,uploaded_change,downloaded_change)
		void record_torrent(std::string &record); // (id,seeders,leechers,snatched_change,balance)
		void record_snatch(std::string &record); // (uid,fid,tstamp)
		void record_peer(std::string &record, std::string &ip, std::string &peer_id, std::string &useragent); // (uid,fid,active,peerid,useragent,ip,uploaded,downloaded,upspeed,downspeed,left,timespent,announces)
		
		void flush_users();
		void flush_torrents();
		void flush_snatches();
		void flush_peers();
		
		void do_flush_users();
		void do_flush_torrents();
		void do_flush_snatches();
		void do_flush_peers();

		boost::mutex torrent_list_mutex;
};
#pragma GCC visibility pop
#endif
