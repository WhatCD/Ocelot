#include <cerrno>
#include "ocelot.h"
#include "config.h"
#include "db.h"
#include "worker.h"
#include "schedule.h"
#include "response.h"
#include "events.h"

// Define the connection mother (first half) and connection middlemen (second half)

//TODO Better errors

//---------- Connection mother - spawns middlemen and lets them deal with the connection

connection_mother::connection_mother(config * conf, worker * worker_obj, mysql * db_obj, site_comm * sc_obj, schedule * sched) : work(worker_obj), db(db_obj) {
	// Handle config stuff first
	load_config(conf);

	listen_socket = create_listen_socket();

	listen_event.set<connection_mother, &connection_mother::handle_connect>(this);
	listen_event.start(listen_socket, ev::READ);
	// Create libev timer
	schedule_event.set<schedule, &schedule::handle>(sched);
	schedule_event.start(sched->schedule_interval, sched->schedule_interval); // After interval, every interval
}

void connection_mother::load_config(config * conf) {
	listen_port = conf->get_uint("listen_port");
	max_connections = conf->get_uint("max_connections");
	max_middlemen = conf->get_uint("max_middlemen");
	connection_timeout = conf->get_uint("connection_timeout");
	keepalive_timeout = conf->get_uint("keepalive_timeout");
	max_read_buffer = conf->get_uint("max_read_buffer");
	max_request_size = conf->get_uint("max_request_size");
}

void connection_mother::reload_config(config * conf) {
	unsigned int old_listen_port = listen_port;
	unsigned int old_max_connections = max_connections;
	load_config(conf);
	if (old_listen_port != listen_port) {
		std::cout << "Changing listen port from " << old_listen_port << " to " << listen_port << std::endl;
		int new_listen_socket = create_listen_socket();
		if (new_listen_socket != 0) {
			listen_event.stop();
			listen_event.start(new_listen_socket, ev::READ);
			close(listen_socket);
			listen_socket = new_listen_socket;
		} else {
			std::cout << "Couldn't create new listen socket when reloading config" << std::endl;
		}
	} else if (old_max_connections != max_connections) {
		listen(listen_socket, max_connections);
	}
}

int connection_mother::create_listen_socket() {
	sockaddr_in address;
	memset(&address, 0, sizeof(address));
	int new_listen_socket = socket(AF_INET, SOCK_STREAM, 0);

	// Stop old sockets from hogging the port
	int yes = 1;
	if (setsockopt(new_listen_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		std::cout << "Could not reuse socket: " << strerror(errno) << std::endl;
		return 0;
	}

	// Get ready to bind
	address.sin_family = AF_INET;
	//address.sin_addr.s_addr = inet_addr(conf->host.c_str()); // htonl(INADDR_ANY)
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(listen_port);

	// Bind
	if (bind(new_listen_socket, (sockaddr *) &address, sizeof(address)) == -1) {
		std::cout << "Bind failed: " << strerror(errno) << std::endl;
		return 0;
	}

	// Listen
	if (listen(new_listen_socket, max_connections) == -1) {
		std::cout << "Listen failed: " << strerror(errno) << std::endl;
		return 0;
	}

	// Set non-blocking
	int flags = fcntl(new_listen_socket, F_GETFL);
	if (flags == -1) {
		std::cout << "Could not get socket flags: " << strerror(errno) << std::endl;
		return 0;
	}
	if (fcntl(new_listen_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
		std::cout << "Could not set non-blocking: " << strerror(errno) << std::endl;
		return 0;
	}

	return new_listen_socket;
}

void connection_mother::run() {
	std::cout << "Sockets up on port " << listen_port << ", starting event loop!" << std::endl;
	ev_loop(ev_default_loop(0), 0);
}

void connection_mother::handle_connect(ev::io &watcher, int events_flags) {
	// Spawn a new middleman
	if (stats.open_connections < max_middlemen) {
		stats.opened_connections++;
		stats.open_connections++;
		new connection_middleman(listen_socket, work, this);
	}
}

connection_mother::~connection_mother()
{
	close(listen_socket);
}







//---------- Connection middlemen - these little guys live until their connection is closed

connection_middleman::connection_middleman(int &listen_socket, worker * new_work, connection_mother * mother_arg) :
	written(0), mother(mother_arg), work(new_work)
{
	connect_sock = accept(listen_socket, NULL, NULL);
	if (connect_sock == -1) {
		std::cout << "Accept failed, errno " << errno << ": " << strerror(errno) << std::endl;
		delete this;
		return;
	}

	// Set non-blocking
	int flags = fcntl(connect_sock, F_GETFL);
	if (flags == -1) {
		std::cout << "Could not get connect socket flags" << std::endl;
	}
	if (fcntl(connect_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
		std::cout << "Could not set non-blocking" << std::endl;
	}

	// Get their info
	request.reserve(mother->max_read_buffer);
	written = 0;

	read_event.set<connection_middleman, &connection_middleman::handle_read>(this);
	read_event.start(connect_sock, ev::READ);

	// Let the socket timeout in timeout_interval seconds
	timeout_event.set<connection_middleman, &connection_middleman::handle_timeout>(this);
	timeout_event.set(mother->connection_timeout, mother->keepalive_timeout);
	timeout_event.start();
}

connection_middleman::~connection_middleman() {
	close(connect_sock);
	stats.open_connections--;
}

// Handler to read data from the socket, called by event loop when socket is readable
void connection_middleman::handle_read(ev::io &watcher, int events_flags) {
	char buffer[mother->max_read_buffer + 1];
	memset(buffer, 0, mother->max_read_buffer + 1);
	int ret = recv(connect_sock, &buffer, mother->max_read_buffer, 0);

	if (ret <= 0) {
		delete this;
		return;
	}
	stats.bytes_read += ret;
	request.append(buffer, ret);
	size_t request_size = request.size();
	if (request_size > mother->max_request_size || (request_size >= 4 && request.compare(request_size - 4, std::string::npos, "\r\n\r\n") == 0)) {
		stats.requests++;
		read_event.stop();
		client_opts.gzip = false;
		client_opts.html = false;
		client_opts.http_close = true;

		if (request_size > mother->max_request_size) {
			shutdown(connect_sock, SHUT_RD);
			response = error("GET string too long", client_opts);
		} else {
			char ip[INET_ADDRSTRLEN];
			sockaddr_in client_addr;
			socklen_t addr_len = sizeof(client_addr);
			getpeername(connect_sock, (sockaddr *) &client_addr, &addr_len);
			inet_ntop(AF_INET, &(client_addr.sin_addr), ip, INET_ADDRSTRLEN);
			std::string ip_str = ip;

			//--- CALL WORKER
			response = work->work(request, ip_str, client_opts);
			request.clear();
			request_size = 0;
		}

		// Find out when the socket is writeable.
		// The loop in connection_mother will call handle_write when it is.
		write_event.set<connection_middleman, &connection_middleman::handle_write>(this);
		write_event.start(connect_sock, ev::WRITE);
	}
}

// Handler to write data to the socket, called by event loop when socket is writeable
void connection_middleman::handle_write(ev::io &watcher, int events_flags) {
	int ret = send(connect_sock, response.c_str()+written, response.size()-written, MSG_NOSIGNAL);
	if (ret == -1) {
		return;
	}
	stats.bytes_written += ret;
	written += ret;
	if (written == response.size()) {
		write_event.stop();
		if (client_opts.http_close) {
			timeout_event.stop();
			delete this;
			return;
		}
		timeout_event.again();
		read_event.start();
		response.clear();
		written = 0;
	}
}

// After a middleman has been alive for timout_interval seconds, this is called
void connection_middleman::handle_timeout(ev::timer &watcher, int events_flags) {
	timeout_event.stop();
	read_event.stop();
	write_event.stop();
	delete this;
}
