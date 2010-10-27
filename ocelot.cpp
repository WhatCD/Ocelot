#include "ocelot.h"
#include "config.h"
#include "db.h"
#include "worker.h"
#include "events.h"
#include "schedule.h"

static mysql *db_ptr;
static connection_mother *mother;

static void sig_handler(int sig)
{
	std::cout << "Caught SIGINT/SIGTERM" << std::endl;
	delete(mother);
	db_ptr->flush_torrents();
	db_ptr->flush_users();
	db_ptr->flush_snatches();
	db_ptr->flush_peers();
	exit(0);
}

int main() {
	config conf;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	
	mysql db(conf.mysql_db, conf.mysql_host, conf.mysql_username, conf.mysql_password);
	db_ptr = &db;
	
	std::unordered_map<std::string, torrent> torrents_list;
	db.load_torrents(torrents_list);
	std::cout << "Loaded " << torrents_list.size() << " torrents" << std::endl;
	
	std::unordered_map<std::string, user> users_list;
	db.load_users(users_list);
	std::cout << "Loaded " << users_list.size() << " users" << std::endl;
	
	std::vector<std::string> whitelist;
	db.load_whitelist(whitelist);
	std::cout << "Loaded " << whitelist.size() << " clients into the whitelist" << std::endl;
	
	
	// Create worker object, which handles announces and scrapes and all that jazz
	worker work(torrents_list, users_list, whitelist, &conf, &db);
	
	// Create connection mother, which binds to its socket and handles the event stuff
	mother = new connection_mother(&work, &conf, &db);

	return 0;
}
