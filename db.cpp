#include "ocelot.h"
#include "db.h"
#include <string>
#include <iostream>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>

mysql::mysql(std::string mysql_db, std::string mysql_host, std::string username, std::string password) {
	if(!conn.connect(mysql_db.c_str(), mysql_host.c_str(), username.c_str(), password.c_str(), 0)) {
		std::cout << "Could not connect to MySQL" << std::endl;
		return;
	}
	
	update_user_buffer = "";
	update_torrent_buffer = "";
	update_peer_buffer = "";
	update_snatch_buffer = "";
}

void mysql::load_torrents(std::unordered_map<std::string, torrent> &torrents) {
	mysqlpp::Query query = conn.query("SELECT ID, info_hash, freetorrent, Snatched FROM torrents ORDER BY ID;");
	if(mysqlpp::StoreQueryResult res = query.store()) {
		mysqlpp::String one("1"); // Hack to get around bug in mysql++3.0.0
		size_t num_rows = res.num_rows();
		for(size_t i = 0; i < num_rows; i++) {
			std::string info_hash;
			res[i][1].to_string(info_hash);
			
			torrent t;
			t.id = res[i][0];
			if(res[i][2].compare(one) == 0) {
				t.free_torrent = true;
			} else {
				t.free_torrent = false;
			}
			t.balance = 0;
			t.completed = res[i][3];
			t.last_selected_seeder = "";
			torrents[info_hash] = t;
		}
	}
}

void mysql::load_users(std::unordered_map<std::string, user> &users) {
	mysqlpp::Query query = conn.query("SELECT ID, can_leech, torrent_pass FROM users_main WHERE Enabled='1';");
	if(mysqlpp::StoreQueryResult res = query.store()) {
		size_t num_rows = res.num_rows();
		for(size_t i = 0; i < num_rows; i++) {
			std::string passkey;
			res[i][2].to_string(passkey);
			
			user u;
			u.id = res[i][0];
			u.can_leech = res[i][1];
			users[passkey] = u;
		}
	}
}

void mysql::load_whitelist(std::vector<std::string> &whitelist) {
	mysqlpp::Query query = conn.query("SELECT peer_id FROM xbt_client_whitelist;");
	if(mysqlpp::StoreQueryResult res = query.store()) {
		size_t num_rows = res.num_rows();
		for(size_t i = 0; i<num_rows; i++) {
			whitelist.push_back(res[i][0].c_str());
		}
	}
}




void mysql::record_user(std::string &record) {
	boost::mutex::scoped_lock lock(user_buffer_lock);
	if(update_user_buffer != "") {
		update_user_buffer += ",";
	}
	update_user_buffer += record;
}
void mysql::record_torrent(std::string &record) {
	boost::mutex::scoped_lock lock(torrent_buffer_lock);
	if(update_torrent_buffer != "") {
		update_torrent_buffer += ",";
	}
	update_torrent_buffer += record;
}
void mysql::record_peer(std::string &record, std::string &ip, std::string &peer_id, std::string &useragent) {
	boost::mutex::scoped_lock lock(peer_buffer_lock);
	if(update_peer_buffer != "") {
		update_peer_buffer += ",";
	}
	mysqlpp::Query q = conn.query();
	q << record << mysqlpp::quote << ip << ',' << mysqlpp::quote << peer_id << ',' << mysqlpp::quote << useragent << ')';
	
	update_peer_buffer += q.str();
}
void mysql::record_snatch(std::string &record) {
	boost::mutex::scoped_lock lock(snatch_buffer_lock);
	if(update_snatch_buffer != "") {
		update_snatch_buffer += ",";
	}
	update_snatch_buffer += record;
}




void mysql::flush_users() {
	boost::thread thread(&mysql::do_flush_users, this);
}

void mysql::do_flush_users() {
	std::cout << "flushing users" << std::endl;
	std::string sql;
	{ // Lock mutex
		boost::mutex::scoped_lock lock(user_buffer_lock);
		if(update_user_buffer == "") {
			return; 
		} else {
			sql = "INSERT INTO users_main(ID, Uploaded, Downloaded) VALUES ";
			sql += update_user_buffer;
			sql += " ON DUPLICATE KEY UPDATE Uploaded=Uploaded+VALUES(Uploaded), Downloaded=Downloaded+VALUES(Downloaded)";
			update_user_buffer.clear();
		}
	}
	
	boost::mutex::scoped_lock db_lock(db_mutex);
	
	
	mysqlpp::Query query = conn.query(sql);
	for(int i = 0; i < 3; i++) {
		try {
			query.execute();
			break;
		} catch(const mysqlpp::BadQuery& er) { // deadlock
			std::cout << "Query error: " << er.what() << std::endl;
			sleep(3);
		} catch (const mysqlpp::Exception& er) { // Weird unpredictable shit
			std::cout << "Query error: " << er.what() << std::endl;
			sleep(3);
		}
	}
	std::cout << "flushed users" << std::endl;
}


void mysql::flush_torrents() {
	boost::thread thread(&mysql::do_flush_torrents, this);
}

void mysql::do_flush_torrents() {
	std::cout << "flushing torrents" << std::endl;
	std::string sql;
	{ // Lock mutex
		boost::mutex::scoped_lock lock(torrent_buffer_lock);
		if(update_torrent_buffer == "") {
			return; 
		} else {
			sql = "INSERT INTO torrents(ID,Seeders,Leechers,Snatched,Balance) VALUES ";
			sql += update_torrent_buffer;
			sql += " ON DUPLICATE KEY UPDATE Seeders=VALUES(Seeders), Leechers=VALUES(Leechers), Snatched=Snatched+VALUES(Snatched), Balance=VALUES(Balance), last_action = IF(VALUES(Seeders) > 0, NOW(), last_action)";
			update_torrent_buffer.clear();
		}
	}
	
	boost::mutex::scoped_lock db_lock(db_mutex);
	
	mysqlpp::Query query = conn.query(sql);
	for(int i = 0; i < 3; i++) {
		try {
			query.execute();
			break;
		} catch(const mysqlpp::BadQuery& er) { // deadlock
			std::cout << "Query error: " << er.what() << std::endl;
			sleep(3);
		} catch (const mysqlpp::Exception& er) { // Weird unpredictable shit
			std::cout << "Query error: " << er.what() << std::endl;
			sleep(3);
		}
	}
	std::cout << "flushed torrents" << std::endl;

	sql = "DELETE FROM torrents WHERE info_hash = ''";
	mysqlpp::Query dquery = conn.query(sql);
	for (int i = 0; i < 3; i++) {
		try {
			dquery.execute();
			break;
		} catch (const mysqlpp::BadQuery& er) {
			std::cout << "Query error: " << er.what() << std::endl;
			sleep(3);
		} catch (const mysqlpp::Exception& er) {
			std::cout << "Query error: " << er.what() << std::endl;
			sleep(3);
		}
	}
}

void mysql::flush_snatches() {
	boost::thread thread(&mysql::do_flush_snatches, this);
}

void mysql::do_flush_snatches() {
	std::cout << "flushing snatches" << std::endl;
	std::string sql;
	{ // lock mutex
		boost::mutex::scoped_lock lock(snatch_buffer_lock);
		if(update_snatch_buffer == "") {
			return; 
		} else {
			sql = "INSERT INTO xbt_snatched(uid,fid,tstamp,IP) VALUES ";
			sql += update_snatch_buffer;
			update_snatch_buffer.clear();
		}
	}
	
	boost::mutex::scoped_lock db_lock(db_mutex);
	
	mysqlpp::Query query = conn.query(sql);
	for(int i = 0; i < 3; i++) {
		try {
			query.execute();
			break;
		} catch(const mysqlpp::BadQuery& er) { // deadlock
			std::cout << "Query error: " << er.what() << std::endl;
			sleep(3);
		} catch (const mysqlpp::Exception& er) { // Weird unpredictable shit
			std::cout << "Query error: " << er.what() << std::endl;
			sleep(3);
		}
	}
	std::cout << "flushed snatches" << std::endl;
}

void mysql::flush_peers() {
	boost::thread thread(&mysql::do_flush_peers, this);
}

void mysql::do_flush_peers() {
	std::cout << "flushing peers" << std::endl;
	std::string sql;
	{ // lock mutex
		boost::mutex::scoped_lock lock(peer_buffer_lock);
		if(update_peer_buffer == "") {
			return; 
		} else {
			sql = "INSERT INTO xbt_files_users(uid,fid,active,uploaded,downloaded,upspeed,downspeed,remaining,timespent,announced,ip,peer_id,useragent) VALUES ";
			sql += update_peer_buffer;
			sql += " ON DUPLICATE KEY UPDATE active=VALUES(active), uploaded=VALUES(uploaded), downloaded=VALUES(downloaded), upspeed=VALUES(upspeed), downspeed=VALUES(downspeed), ";
			sql += "remaining=VALUES(remaining), timespent=VALUES(timespent), announced=VALUES(announced), peer_id=VALUES(peer_id), useragent=VALUES(useragent),mtime=UNIX_TIMESTAMP(NOW())";		
			update_peer_buffer.clear();
		}
	}
	
	boost::mutex::scoped_lock db_lock(db_mutex);
	
	mysqlpp::Query query = conn.query(sql);
	for(int i = 0; i < 3; i++) {
		try {
			query.execute();
			break;
		} catch(const mysqlpp::BadQuery& er) { // deadlock
			std::cout << "Query error: " << er.what() << std::endl;
			sleep(3);
		} catch (const mysqlpp::Exception& er) { // Weird unpredictable shit
			std::cout << "Query error: " << er.what() << std::endl;
			sleep(3);
		}
	}
	std::cout << "flushed peers" << std::endl;
}
