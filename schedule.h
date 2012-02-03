#include <ev++.h>
#include <string>
#include <iostream>

class schedule {
	private:
		connection_mother * mother;
		worker * work;
		config * conf;
		mysql * db;
		int last_opened_connections;
		int counter;
		
		time_t next_flush;
		time_t next_reap_peers;
	public:
		schedule(connection_mother * mother_obj, worker * worker_obj, config* conf_obj, mysql * db_obj);
		void handle(ev::timer &watcher, int events_flags);
};
