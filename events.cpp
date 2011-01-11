#include "ocelot.h"
#include "config.h"
#include "db.h"
#include "worker.h"
#include "events.h"
#include "schedule.h"
#include <cerrno>



// Define the connection mother (first half) and connection middlemen (second half)

//TODO Better errors

//---------- Connection mother - spawns middlemen and lets them deal with the connection

connection_mother::connection_mother(worker * worker_obj, config * config_obj, mysql * db_obj) : work(worker_obj), conf(config_obj), db(db_obj) {
	open_connections = 0;
	opened_connections = 0;
	
	memset(&address, 0, sizeof(address));
	addr_len = sizeof(address);
	
	listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	
	// Stop old sockets from hogging the port
	int yes = 1;
	if(setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		std::cout << "Could not reuse socket" << std::endl;
	}
	
	// Create libev event loop
	ev::io event_loop_watcher;
	
	event_loop_watcher.set<connection_mother, &connection_mother::handle_connect>(this);
	event_loop_watcher.start(listen_socket, ev::READ);
	
	// Get ready to bind
	address.sin_family = AF_INET;
	//address.sin_addr.s_addr = inet_addr(conf->host.c_str()); // htonl(INADDR_ANY)
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(conf->port);
	
	// Bind
	if(bind(listen_socket, (sockaddr *) &address, sizeof(address)) == -1) {
		std::cout << "Bind failed " << errno << std::endl;
	}
	
	// Listen
	if(listen(listen_socket, conf->max_connections) == -1) {
		std::cout << "Listen failed" << std::endl;
	}
	
	// Set non-blocking
	int flags = fcntl(listen_socket, F_GETFL);
	if(flags == -1) {
		std::cout << "Could not get socket flags" << std::endl;
	}
	if(fcntl(listen_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
		std::cout << "Could not set non-blocking" << std::endl;
	}
	
	// Create libev timer
	schedule timer(this, worker_obj, conf, db);
	
	schedule_event.set<schedule, &schedule::handle>(&timer);
	schedule_event.set(conf->schedule_interval, conf->schedule_interval); // After interval, every interval
	schedule_event.start();
	
	std::cout << "Sockets up, starting event loop!" << std::endl;
	ev_loop(ev_default_loop(0), 0);
}


void connection_mother::handle_connect(ev::io &watcher, int events_flags) {
	// Spawn a new middleman
	if(open_connections < conf->max_middlemen) {
		opened_connections++;
		new connection_middleman(listen_socket, address, addr_len, work, this, conf);
	}
}

connection_mother::~connection_mother()
{
	close(listen_socket);
}







//---------- Connection middlemen - these little guys live until their connection is closed

connection_middleman::connection_middleman(int &listen_socket, sockaddr_in &address, socklen_t &addr_len, worker * new_work, connection_mother * mother_arg, config * config_obj) : 
	conf(config_obj), mother (mother_arg), work(new_work) {
	
	connect_sock = accept(listen_socket, (sockaddr *) &address, &addr_len);
	if(connect_sock == -1) {
		std::cout << "Accept failed, errno " << errno << ": " << strerror(errno) << std::endl;
		mother->increment_open_connections(); // destructor decrements open connections
		delete this;
		return;
	}
	
	// Set non-blocking
	int flags = fcntl(connect_sock, F_GETFL);
	if(flags == -1) {
		std::cout << "Could not get connect socket flags" << std::endl;
	}
	if(fcntl(connect_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
		std::cout << "Could not set non-blocking" << std::endl;
	}
	
	// Get their info
	if(getpeername(connect_sock, (sockaddr *) &client_addr, &addr_len) == -1) {
		//std::cout << "Could not get client info" << std::endl;
	}
	
	
	read_event.set<connection_middleman, &connection_middleman::handle_read>(this);
	read_event.start(connect_sock, ev::READ);
	
	// Let the socket timeout in timeout_interval seconds
	timeout_event.set<connection_middleman, &connection_middleman::handle_timeout>(this);
	timeout_event.set(conf->timeout_interval, 0);
	timeout_event.start();
	
	mother->increment_open_connections();
}

connection_middleman::~connection_middleman() {
	close(connect_sock);
	mother->decrement_open_connections();
}

// Handler to read data from the socket, called by event loop when socket is readable
void connection_middleman::handle_read(ev::io &watcher, int events_flags) {
	read_event.stop();
	
	char buffer[conf->max_read_buffer + 1];
	memset(buffer, 0, conf->max_read_buffer + 1);
	int status = recv(connect_sock, &buffer, conf->max_read_buffer, 0);
	
	if(status == -1) {
		delete this;
		return;
	}
	
	std::string stringbuf = buffer;
	
	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(client_addr.sin_addr), ip, INET_ADDRSTRLEN);
	std::string ip_str = ip;
	
	//--- CALL WORKER
	response = work->work(stringbuf, ip_str);
	
	// Find out when the socket is writeable. 
	// The loop in connection_mother will call handle_write when it is. 
	write_event.set<connection_middleman, &connection_middleman::handle_write>(this);
	write_event.start(connect_sock, ev::WRITE);
}

// Handler to write data to the socket, called by event loop when socket is writeable
void connection_middleman::handle_write(ev::io &watcher, int events_flags) {
	write_event.stop();
	timeout_event.stop();
	std::string http_response = "HTTP/1.1 200\r\nServer: Ocelot 1.0\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n";
	http_response+=response;
	send(connect_sock, http_response.c_str(), http_response.size(), MSG_NOSIGNAL);
	delete this;
}

// After a middleman has been alive for timout_interval seconds, this is called
void connection_middleman::handle_timeout(ev::timer &watcher, int events_flags) {
	timeout_event.stop();
	read_event.stop();
	write_event.stop();
	delete this;
}
