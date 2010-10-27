#include "ocelot.h"
#include "config.h"
#include "db.h"
#include "worker.h"
#include "events.h"
#include "schedule.h"


schedule::schedule(connection_mother * mother_obj, worker* worker_obj, config* conf_obj, mysql * db_obj) : mother(mother_obj), work(worker_obj), conf(conf_obj), db(db_obj) {
	counter = 0;
	last_opened_connections = 0;
	
	next_flush_torrents = time(NULL) + conf->flush_torrents_interval;
	next_flush_users = time(NULL) + conf->flush_users_interval + 10;
	next_flush_peers = time(NULL) + conf->flush_peers_interval + 20;
	next_flush_snatches = time(NULL) + conf->flush_snatches_interval + 30;
	next_reap_peers = time(NULL) + conf->reap_peers_interval + 40;
}
//---------- Schedule - gets called every schedule_interval seconds
void schedule::handle(ev::timer &watcher, int events_flags) {
	
	if(counter % 20 == 0) {
		std::cout << "Scedule run #" << counter << " - open: " << mother->get_open_connections() << ", opened: " 
		<< mother->get_opened_connections() << ", speed: "
		<< ((mother->get_opened_connections()-last_opened_connections)/conf->schedule_interval) << "/s" << std::endl;
	}

	last_opened_connections = mother->get_opened_connections();
	
	time_t cur_time = time(NULL);
	if(cur_time > next_flush_torrents) {
		db->flush_torrents();
		next_flush_torrents = cur_time + conf->flush_torrents_interval;
	}
	
	if(cur_time > next_flush_users) {
		db->flush_users();
		next_flush_users = cur_time + conf->flush_users_interval;
	}
	
	if(cur_time > next_flush_snatches) {
		db->flush_snatches();
		next_flush_snatches = cur_time + conf->flush_snatches_interval;
	}
	
	if(cur_time > next_flush_peers) {
		db->flush_peers();
		next_flush_peers = cur_time + conf->flush_peers_interval;
	}

	if(cur_time > next_reap_peers) {
		work->reap_peers();
		next_reap_peers = cur_time + conf->reap_peers_interval;
	}

	counter++;
}
