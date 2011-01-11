#include <cmath>
#include <iostream>
#include <string>
#include <map>
#include <sstream>
#include <list>
#include <vector>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "ocelot.h"
#include "config.h"
#include "db.h"
#include "worker.h"
#include "misc_functions.h"

#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/locks.hpp>
#include <boost/bind.hpp>

//---------- Worker - does stuff with input
worker::worker(torrent_list &torrents, user_list &users, std::vector<std::string> &_whitelist, config * conf_obj, mysql * db_obj) : torrents_list(torrents), users_list(users), whitelist(_whitelist), conf(conf_obj), db(db_obj) {
}
std::string worker::work(std::string &input, std::string &ip) {
	unsigned int input_length = input.length();
	
	//---------- Parse request - ugly but fast. Using substr exploded.
	if(input_length < 60) { // Way too short to be anything useful
		return error("GET string too short");
	}
	
	size_t pos = 5; // skip GET /
	
	// Get the passkey
	std::string passkey;
	passkey.reserve(32);
	if(input[37] != '/') {
		return error("Malformed announce");
	}
	
	for(; pos < 37; pos++) {
		passkey.push_back(input[pos]);
	}
	
	pos = 38;
	
	// Get the action
	enum action_t {
		INVALID = 0, ANNOUNCE, SCRAPE, UPDATE
	};
	action_t action = INVALID;
	
	switch(input[pos]) {
		case 'a':
			action = ANNOUNCE;
			pos += 9;
			break;
		case 's':
			action = SCRAPE;
			pos += 7;
			break;
		case 'u':
			action = UPDATE;
			pos += 7;
			break;
	}
	if(action == INVALID) {
		return error("invalid action");
	}
	
	// Parse URL params
	std::list<std::string> infohashes; // For scrape only
	
	std::map<std::string, std::string> params;
	std::string key;
	std::string value;
	bool parsing_key = true; // true = key, false = value
	
	for(; pos < input_length; ++pos) {
		if(input[pos] == '=') {
			parsing_key = false;
		} else if(input[pos] == '&' || input[pos] == ' ') {
			parsing_key = true;
			if(action == SCRAPE && key == "info_hash") {
				infohashes.push_back(value);
			} else {
				params[key] = value;
			}
			key.clear();
			value.clear();
			if(input[pos] == ' ') {
				break;
			}
		} else {
			if(parsing_key) {
				key.push_back(input[pos]);
			} else {
				value.push_back(input[pos]);
			}
		}
	}
	
	pos += 10; // skip HTTP/1.1 - should probably be +=11, but just in case a client doesn't send \r
	
	// Parse headers
	std::map<std::string, std::string> headers;
	parsing_key = true;
	bool found_data = false;
	
	for(; pos < input_length; ++pos) {
		if(input[pos] == ':') {
			parsing_key = false;
			++pos; // skip space after :
		} else if(input[pos] == '\n' || input[pos] == '\r') {
			parsing_key = true;
			
			if(found_data) {
				found_data = false; // dodge for getting around \r\n or just \n
				headers[key] = value;
				key.clear();
				value.clear();
			}
		} else {
			found_data = true;
			if(parsing_key) {
				key.push_back(input[pos]);
			} else {
				value.push_back(input[pos]);
			}
		}
	}
	
	
	
	if(action == UPDATE) {
		if(passkey == conf->site_password) {
			return update(params);
		} else {
			return error("Authentication failure");
		}
	}
	
	// Either a scrape or an announce
	
	user_list::iterator u = users_list.find(passkey);
	if(u == users_list.end()) {
		return error("passkey not found");
	}
	
	if(action == ANNOUNCE) {
		boost::mutex::scoped_lock lock(db->torrent_list_mutex);
		// Let's translate the infohash into something nice
		// info_hash is a url encoded (hex) base 20 number
		std::string info_hash_decoded = hex_decode(params["info_hash"]);
		torrent_list::iterator tor = torrents_list.find(info_hash_decoded);
		if(tor == torrents_list.end()) {
			return error("unregistered torrent");
		}
		return announce(tor->second, u->second, params, headers, ip);
	} else {
		return scrape(infohashes);
	}
}

std::string worker::error(std::string err) {
	std::string output = "d14:failure reason";
	output += inttostr(err.length());
	output += ':';
	output += err;
	output += 'e';
	return output;
}

std::string worker::announce(torrent &tor, user &u, std::map<std::string, std::string> &params, std::map<std::string, std::string> &headers, std::string &ip){
	time_t cur_time = time(NULL);
	
	if(params["compact"] != "1") {
		return error("Your client does not support compact announces");
	}
	
	long long left = strtolonglong(params["left"]);
	long long uploaded = std::max(0ll, strtolonglong(params["uploaded"]));
	long long downloaded = std::max(0ll, strtolonglong(params["downloaded"]));
	
	bool inserted = false; // If we insert the peer as opposed to update
	bool update_torrent = false; // Whether or not we should update the torrent in the DB
	
	std::map<std::string, std::string>::const_iterator peer_id_iterator = params.find("peer_id");
	if(peer_id_iterator == params.end()) {
		return error("no peer id");
	}
	std::string peer_id = peer_id_iterator->second;
	peer_id = hex_decode(peer_id);
	
	bool found = false; // Found client in whitelist?
	for(unsigned int i = 0; i < whitelist.size(); i++) {
		if(peer_id.find(whitelist[i]) == 0) {
			found = true;
			break;
		}
	}

	if(!found) {
		return error("Your client is not on the whitelist");
	}
	
	
	peer * p;
	peer_list::iterator i;
	// Insert/find the peer in the torrent list
	if(left > 0 || params["event"] == "completed") {
		if(u.can_leech == false) {
			return error("Access denied, leeching forbidden");
		}
		
		i = tor.leechers.find(peer_id);
		if(i == tor.leechers.end()) {
			peer new_peer;
			std::pair<peer_list::iterator, bool> insert 
			= tor.leechers.insert(std::pair<std::string, peer>(peer_id, new_peer));
			
			p = &(insert.first->second);
			inserted = true;
		} else {
			p = &i->second;
		}
	} else {
		i = tor.seeders.find(peer_id);
		if(i == tor.seeders.end()) {
			peer new_peer;
			std::pair<peer_list::iterator, bool> insert 
			= tor.seeders.insert(std::pair<std::string, peer>(peer_id, new_peer));
			
			p = &(insert.first->second);
			inserted = true;
		} else {
			p = &i->second;
		}
		
		tor.last_seeded = cur_time;
	}
	
	// Update the peer
	p->left = left;
	long long upspeed = 0;
	long long downspeed = 0;
	
	if(inserted || params["event"] == "started" || uploaded < p->uploaded || downloaded < p->downloaded) {
		//New peer on this torrent
		update_torrent = true;
		p->userid = u.id;
		p->peer_id = peer_id;
		p->user_agent = headers["User-Agent"];
		p->first_announced = cur_time;
		p->last_announced = 0;
		p->uploaded = uploaded;
		p->downloaded = downloaded;
		p->announces = 1;
	} else {
		p->announces++;
		
		long long uploaded_change = 0;
		long long downloaded_change = 0;
		if(uploaded != p->uploaded) {
			uploaded_change = uploaded - p->uploaded;
			p->uploaded = uploaded;
		}
		if(downloaded != p->downloaded) {
			downloaded_change = downloaded - p->downloaded;
			p->downloaded = downloaded;
		}
		if(uploaded_change || downloaded_change) {
			long corrupt = strtolong(params["corrupt"]);
			tor.balance += uploaded_change;
			tor.balance -= downloaded_change;
			tor.balance -= corrupt;
			update_torrent = true;
			
			if(cur_time > p->last_announced) {
				upspeed = uploaded_change / (cur_time - p->last_announced);
				downspeed = downloaded_change / (cur_time - p->last_announced);
			}
			
			if(tor.free_torrent == true) {
				downloaded_change = 0;
			}
			
			if(uploaded_change || downloaded_change) {
				
				std::stringstream record;
				record << '(' << u.id << ',' << uploaded_change << ',' << downloaded_change << ')';
				std::string record_str = record.str();
				db->record_user(record_str);
			}
		}
	}
	p->last_announced = cur_time;
	
	std::map<std::string, std::string>::const_iterator param_ip = params.find("ip");
	if(param_ip != params.end()) {
		ip = param_ip->second;
	} else {
		param_ip = params.find("ipv4");
		if(param_ip != params.end()) {
			ip = param_ip->second;
		}
	}
	
	unsigned int port = strtolong(params["port"]);
	// Generate compact ip/port string
	if(inserted || port != p->port || ip != p->ip) {
		p->port = port;
		p->ip = ip;
		p->ip_port = "";
		char x = 0;
		for(size_t pos = 0, end = ip.length(); pos < end; pos++) {
			if(ip[pos] == '.') {
				p->ip_port.push_back(x);
				x = 0;
				continue;
			} else if(!isdigit(ip[pos])) {
				return error("Unexpected character in IP address. Only IPv4 is currently supported");
			}
			x = x * 10 + ip[pos] - '0';
		}
		p->ip_port.push_back(x);
		p->ip_port.push_back(port >> 8);
		p->ip_port.push_back(port & 0xFF);
		if(p->ip_port.length() != 6) {
			return error("Specified IP address is of a bad length");
		}
	}
	
	// Select peers!
	unsigned int numwant;
	std::map<std::string, std::string>::const_iterator param_numwant = params.find("numwant");
	if(param_numwant == params.end()) {
		numwant = 50;
	} else {
		numwant = std::min(50l, strtolong(param_numwant->second));
	}

	int snatches = 0;
	int active = 1;
	if(params["event"] == "stopped") {
		update_torrent = true;
		active = 0;
		numwant = 0;

		if(left > 0) {
			if(tor.leechers.erase(peer_id) == 0) {
				std::cout << "Tried and failed to remove seeder from torrent " << tor.id << std::endl;
			}
		} else {
			if(tor.seeders.erase(peer_id) == 0) {
				std::cout << "Tried and failed to remove leecher from torrent " << tor.id << std::endl;
			}
		}
	} else if(params["event"] == "completed") {
		snatches = 1;
		update_torrent = true;
		tor.completed++;
		
		std::stringstream record;
		record << '(' << u.id << ',' << tor.id << ',' << cur_time << ", '" << ip << "')";
		std::string record_str = record.str();
		db->record_snatch(record_str);
		
		// User is a seeder now!
		tor.seeders.insert(std::pair<std::string, peer>(peer_id, *p));
		tor.leechers.erase(peer_id);
	}

	std::string peers;
	if(numwant > 0) {
		peers.reserve(300);
		unsigned int found_peers = 0;
		if(left > 0) { // Show seeders to leechers first
			if(tor.seeders.size() > 0) {
				// We do this complicated stuff to cycle through the seeder list, so all seeders will get shown to leechers
				
				// Find out where to begin in the seeder list
				peer_list::const_iterator i;
				if(tor.last_selected_seeder == "") {
					i = tor.seeders.begin();
				} else {
					i = tor.seeders.find(tor.last_selected_seeder);
					i++;
					if(i == tor.seeders.end()) {
						i = tor.seeders.begin();
					}
				}
				
				// Find out where to end in the seeder list
				peer_list::const_iterator end;
				if(i == tor.seeders.begin()) {
					end = tor.seeders.end();
				} else {
					end = i;
					end--;
				}
				
				// Add seeders
				while(i != end && found_peers < numwant) {
					if(i == tor.seeders.end()) {
						i = tor.seeders.begin();
					}
					peers.append(i->second.ip_port);
					found_peers++;
					tor.last_selected_seeder = i->second.peer_id;
					i++;
				}
			}

			if(found_peers < numwant && tor.leechers.size() > 1) {
				for(peer_list::const_iterator i = tor.leechers.begin(); i != tor.leechers.end() && found_peers < numwant; i++) {
					if(i->second.ip_port == p->ip_port) { // Don't show leechers themselves
						continue; 
					}
					found_peers++;
					peers.append(i->second.ip_port);
				}
				
			}
		} else if(tor.leechers.size() > 0) { // User is a seeder, and we have leechers!
			for(peer_list::const_iterator i = tor.leechers.begin(); i != tor.leechers.end() && found_peers < numwant; i++) {
				found_peers++;
				peers.append(i->second.ip_port);
			}
		}
	}
	
	if(update_torrent || tor.last_flushed + 3600 < cur_time) {
		tor.last_flushed = cur_time;
		
		std::stringstream record;
		record << '(' << tor.id << ',' << tor.seeders.size() << ',' << tor.leechers.size() << ',' << snatches << ',' << tor.balance << ')';
		std::string record_str = record.str();
		db->record_torrent(record_str);
	}
	
	std::stringstream record;
	record << '(' << u.id << ',' << tor.id << ',' << active << ',' << uploaded << ',' << downloaded << ',' << upspeed << ',' << downspeed << ',' << left << ',' << (cur_time - p->first_announced) << ',' << p->announces << ',';
	std::string record_str = record.str();
	db->record_peer(record_str, ip, peer_id, headers["User-Agent"]);
	
	std::string response = "d8:intervali";
	response.reserve(350);
	response += inttostr(conf->announce_interval+std::min((size_t)600, tor.seeders.size())); // ensure a more even distribution of announces/second
	response += "e12:min intervali";
	response += inttostr(conf->announce_interval);
	response += "e5:peers";
	if(peers.length() == 0) {
		response += "0:";
	} else {
		response += inttostr(peers.length());
		response += ":";
		response += peers;
	}
	response += "8:completei";
	response += inttostr(tor.seeders.size());
	response += "e10:incompletei";
	response += inttostr(tor.leechers.size());
	response += "e10:downloadedi";
	response += inttostr(tor.completed);
	response += "ee";
	
	return response;
}

std::string worker::scrape(const std::list<std::string> &infohashes) {
	std::string output = "d5:filesd";
	for(std::list<std::string>::const_iterator i = infohashes.begin(); i != infohashes.end(); i++) {
		std::string infohash = *i;
		infohash = hex_decode(infohash);
		
		torrent_list::iterator tor = torrents_list.find(infohash);
		if(tor == torrents_list.end()) {
			continue;
		}
		torrent *t = &(tor->second);
		
		output += inttostr(infohash.length());
		output += ':';
		output += infohash;
		output += "d8:completei";
		output += inttostr(t->seeders.size());
		output += "e10:incompletei";
		output += inttostr(t->leechers.size());
		output += "e10:downloadedi";
		output += inttostr(t->completed);
		output += "ee";
	}
	output+="ee";
	return output;
}

//TODO: Restrict to local IPs
std::string worker::update(std::map<std::string, std::string> &params) {
	std::cout << "Got update" << std::endl;
	if(params["action"] == "change_passkey") {
		std::string oldpasskey = params["oldpasskey"];
		std::string newpasskey = params["newpasskey"];
		users_list[newpasskey] = users_list[oldpasskey];
		users_list.erase(oldpasskey);
		std::cout << "changed passkey from " << oldpasskey << " to " << newpasskey << " for user " << users_list[newpasskey].id << std::endl;
	} else if(params["action"] == "add_torrent") {
		torrent t;
		t.id = strtolong(params["id"]);
		std::string info_hash = params["info_hash"];
		info_hash = hex_decode(info_hash);
		bool fl = false;
		if(params["freetorrent"] == "1") {
			fl = true;
		}
		t.balance = 0;
		t.completed = 0;
		t.last_selected_seeder = "";
		t.free_torrent = fl;
		torrents_list[info_hash] = t;
		std::cout << "Added torrent " << t.id << std::endl;
	} else if(params["action"] == "update_torrent") {
		std::string info_hash = params["info_hash"];
		info_hash = hex_decode(info_hash);
		bool fl = false;
		if(params["freetorrent"] == "1") {
			fl = true;
		}
		if(torrents_list.find(info_hash) != torrents_list.end()) {
			torrents_list[info_hash].free_torrent = fl;
			std::cout << "Updated torrent " << torrents_list[info_hash].id << " to FL " << fl << std::endl;
		} else {
			std::cout << "Failed to find torrent " << info_hash << " to FL " << fl << std::endl;
		}
	} else if(params["action"] == "update_torrents") {
		// Each decoded infohash is exactly 20 characters long.
		std::string info_hashes = params["info_hashes"];
		info_hashes = hex_decode(info_hashes);
		bool fl = false;
		if(params["freetorrent"] == "1") {
			fl = true;
		}
		for(unsigned int pos = 0; pos < info_hashes.length(); pos += 20) {
			std::string info_hash = info_hashes.substr(pos, 20);
			if(torrents_list.find(info_hash) != torrents_list.end()) {
				torrents_list[info_hash].free_torrent = fl;
				std::cout << "Updated torrent " << torrents_list[info_hash].id << " to FL " << fl << std::endl;
			} else {
				std::cout << "Failed to find torrent " << info_hash << " to FL " << fl << std::endl;
			}
		}
	} else if(params["action"] == "delete_torrent") {
		std::string info_hash = params["info_hash"];
		info_hash = hex_decode(info_hash);
		if(torrents_list.find(info_hash) != torrents_list.end()) {
			std::cout << "Deleting torrent " << torrents_list[info_hash].id << std::endl;
			torrents_list.erase(info_hash);
		} else {
			std::cout << "Failed to find torrent " << info_hash << " to delete " << std::endl;
		}
	} else if(params["action"] == "add_user") {
		std::string passkey = params["passkey"];
		unsigned int id = strtolong(params["id"]);
		user u;
		u.id = id;
		u.can_leech = 1;
		users_list[passkey] = u;
		std::cout << "Added user " << id << std::endl;
	} else if(params["action"] == "remove_user") {
		std::string passkey = params["passkey"];
		users_list.erase(passkey);
		std::cout << "Removed user " << passkey << std::endl;
	} else if(params["action"] == "remove_users") {
		// Each passkey is exactly 32 characters long.
		std::string passkeys = params["passkeys"];
		for(unsigned int pos = 0; pos < passkeys.length(); pos += 32){
			std::string passkey = passkeys.substr(pos, 32);
			users_list.erase(passkey);
			std::cout << "Removed user " << passkey << std::endl;
		}
	} else if(params["action"] == "update_user") {
		std::string passkey = params["passkey"];
		bool can_leech = true;
		if(params["can_leech"] == "0") {
			can_leech = false;
		}
		users_list[passkey].can_leech = can_leech;
		std::cout << "Updated user " << passkey << std::endl;
	} else if(params["action"] == "add_whitelist") {
		std::string peer_id = params["peer_id"];
		whitelist.push_back(peer_id);
		std::cout << "Whitelisted " << peer_id << std::endl;
	} else if(params["action"] == "remove_whitelist") {
		std::string peer_id = params["peer_id"];
		for(unsigned int i = 0; i < whitelist.size(); i++) {
			if(whitelist[i].compare(peer_id) == 0) {
				whitelist.erase(whitelist.begin() + i);
				break;
			}
		}
		std::cout << "De-whitelisted " << peer_id << std::endl;
	} else if(params["action"] == "edit_whitelist") {
		std::string new_peer_id = params["new_peer_id"];
		std::string old_peer_id = params["old_peer_id"];
		for(unsigned int i = 0; i < whitelist.size(); i++) {
			if(whitelist[i].compare(old_peer_id) == 0) {
				whitelist.erase(whitelist.begin() + i);
				break;
			}
		}
		whitelist.push_back(new_peer_id);
		std::cout << "Edited whitelist item from " << old_peer_id << " to " << new_peer_id << std::endl;
	} else if(params["action"] == "update_announce_interval") {
		unsigned int interval = strtolong(params["new_announce_interval"]);
		conf->announce_interval = interval;
		std::cout << "Edited announce interval to " << interval << std::endl;
	}
	return "success";
}

void worker::reap_peers() {
	std::cout << "started reaper" << std::endl;
	boost::thread thread(&worker::do_reap_peers, this);
}

void worker::do_reap_peers() {
	time_t cur_time = time(NULL);
	unsigned int reaped = 0;
	std::unordered_map<std::string, torrent>::iterator i = torrents_list.begin();
	for(; i != torrents_list.end(); i++) {
		std::map<std::string, peer>::iterator p = i->second.leechers.begin();
		std::map<std::string, peer>::iterator del_p;
		while(p != i->second.leechers.end()) {
			if(p->second.last_announced + conf->peers_timeout < cur_time) {
				del_p = p;
				p++;
				boost::mutex::scoped_lock lock(db->torrent_list_mutex);
				i->second.leechers.erase(del_p);
				reaped++;
			} else {
				p++;
			}
		}
		p = i->second.seeders.begin();
		while(p != i->second.seeders.end()) {
			if(p->second.last_announced + conf->peers_timeout < cur_time) {
				del_p = p;
				p++;
				boost::mutex::scoped_lock lock(db->torrent_list_mutex);
				i->second.seeders.erase(del_p);
				reaped++;
			} else {
				p++;
			}
		}
	}
	std::cout << "Reaped " << reaped << " peers" << std::endl;
}
