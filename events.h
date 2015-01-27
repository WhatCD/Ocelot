#include <iostream>
#include <string>
#include <cstring>

// libev
#include <ev++.h>

// Sockets
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include "config.h"
#include "worker.h"
#include "schedule.h"
#include "db.h"
#include "site_comm.h"

/*
TODO find out what these do
#include <netinet/in.h>
#include <netdb.h>
#include <sys/types.h>
*/





/*
We have three classes - the mother, the middlemen, and the worker
THE MOTHER
	The mother is called when a client opens a connection to the server.
	It creates a middleman for every new connection, which will be called
	when its socket is ready for reading.
THE MIDDLEMEN
	Each middleman hang around until data is written to its socket. It then
	reads the data and sends it to the worker. When it gets the response, it
	gets called to write its data back to the client.
THE WORKER
	The worker gets data from the middleman, and returns the response. It
	doesn't concern itself with silly things like sockets.

	see worker.h for the worker.
*/




// THE MOTHER - Spawns connection middlemen
class connection_mother {
	private:
		void load_config(config * conf);
		unsigned int listen_port;
		unsigned int max_connections;

		int listen_socket;
		worker * work;
		mysql * db;
		ev::io listen_event;
		ev::timer schedule_event;

	public:
		connection_mother(config * conf, worker * worker_obj, mysql * db_obj, site_comm * sc_obj, schedule * sched_obj);
		~connection_mother();
		void reload_config(config * conf);
		int create_listen_socket();
		void run();
		void handle_connect(ev::io &watcher, int events_flags);

		unsigned int max_middlemen;
		unsigned int connection_timeout;
		unsigned int keepalive_timeout;
		unsigned int max_read_buffer;
		unsigned int max_request_size;
};

// THE MIDDLEMAN
// Created by connection_mother
// Add their own watchers to see when sockets become readable
class connection_middleman {
	private:
		int connect_sock;
		client_opts_t client_opts;
		unsigned int written;
		ev::io read_event;
		ev::io write_event;
		ev::timer timeout_event;
		std::string request;
		std::string response;

		connection_mother * mother;
		worker * work;

	public:
		connection_middleman(int &listen_socket, worker* work, connection_mother * mother_arg);
		~connection_middleman();

		void handle_read(ev::io &watcher, int events_flags);
		void handle_write(ev::io &watcher, int events_flags);
		void handle_timeout(ev::timer &watcher, int events_flags);
};
