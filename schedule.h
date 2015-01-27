#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <ev++.h>
class schedule {
	private:
		void load_config(config * conf);

		unsigned int reap_peers_interval;
		worker * work;
		mysql * db;
		site_comm * sc;
		uint64_t last_opened_connections;
		uint64_t last_request_count;
		unsigned int counter;
		int next_reap_peers;
	public:
		schedule(config * conf, worker * worker_obj, mysql * db_obj, site_comm * sc_obj);
		void reload_config(config * conf);
		void handle(ev::timer &watcher, int events_flags);

		unsigned int schedule_interval;
};
#endif
