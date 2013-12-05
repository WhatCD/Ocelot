#include "ocelot.h"
#include "config.h"
#include "db.h"
#include "worker.h"
#include "events.h"
#include "schedule.h"
#include "site_comm.h"

static connection_mother *mother;
static worker *work;
struct stats stats;

static void sig_handler(int sig)
{
	std::cout << "Caught SIGINT/SIGTERM" << std::endl;
	if (work->signal(sig)) {
		exit(0);
	}
}

int main(int argc, char **argv) {
	// we don't use printf so make cout/cerr a little bit faster
	std::ios_base::sync_with_stdio(false);

	config conf;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	bool verbose = false;
	for (int i = argc; i > 1; i--) {
		if (!strcmp(argv[1], "-v")) {
			verbose = true;
		}
	}
	mysql db(conf.mysql_db, conf.mysql_host, conf.mysql_username, conf.mysql_password);
	db.verbose_flush = verbose;

	site_comm sc(conf);
	sc.verbose_flush = verbose;

	std::vector<std::string> whitelist;
	db.load_whitelist(whitelist);
	std::cout << "Loaded " << whitelist.size() << " clients into the whitelist" << std::endl;
	if (whitelist.size() == 0) {
		std::cout << "Assuming no whitelist desired, disabling" << std::endl;
	}

	user_list users_list;
	db.load_users(users_list);
	std::cout << "Loaded " << users_list.size() << " users" << std::endl;

	torrent_list torrents_list;
	db.load_torrents(torrents_list);
	std::cout << "Loaded " << torrents_list.size() << " torrents" << std::endl;

	db.load_tokens(torrents_list);

	stats.open_connections = 0;
	stats.opened_connections = 0;
	stats.connection_rate = 0;
	stats.leechers = 0;
	stats.seeders = 0;
	stats.announcements = 0;
	stats.succ_announcements = 0;
	stats.scrapes = 0;
	stats.bytes_read = 0;
	stats.bytes_written = 0;
	stats.start_time = time(NULL);

	// Create worker object, which handles announces and scrapes and all that jazz
	work = new worker(torrents_list, users_list, whitelist, &conf, &db, &sc);

	// Create connection mother, which binds to its socket and handles the event stuff
	mother = new connection_mother(work, &conf, &db, &sc);

	return 0;
}
