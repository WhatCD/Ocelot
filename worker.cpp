#include <cmath>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <sstream>
#include <list>
#include <vector>
#include <set>
#include <algorithm>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "ocelot.h"
#include "config.h"
#include "db.h"
#include "worker.h"
#include "misc_functions.h"
#include "site_comm.h"

#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/locks.hpp>
#include <boost/bind.hpp>

//---------- Worker - does stuff with input
worker::worker(torrent_list &torrents, user_list &users, std::vector<std::string> &_whitelist, config * conf_obj, mysql * db_obj, site_comm &sc) : torrents_list(torrents), users_list(users), whitelist(_whitelist), conf(conf_obj), db(db_obj), s_comm(sc) {
	status = OPEN;
}
bool worker::signal(int sig) {
	if (status == OPEN) {
		status = CLOSING;
		std::cout << "closing tracker... press Ctrl-C again to terminate" << std::endl;
		return false;
	} else if (status == CLOSING) {
		std::cout << "shutting down uncleanly" << std::endl;
		return true;
	} else {
		return false;
	}
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

	if ((status != OPEN) && (action != UPDATE)) {
		return error("The tracker is temporarily unavailable.");
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
				std::transform(key.begin(), key.end(), key.begin(), ::tolower);
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
	
	int64_t left = strtolonglong(params["left"]);
	int64_t uploaded = std::max((int64_t)0, strtolonglong(params["uploaded"]));
	int64_t downloaded = std::max((int64_t)0, strtolonglong(params["downloaded"]));
	int64_t corrupt = strtolonglong(params["corrupt"]);
	
	bool inserted = false; // If we insert the peer as opposed to update
	bool update_torrent = false; // Whether or not we should update the torrent in the DB
	bool completed_torrent = false; // Whether or not the current announcement is a snatch
	bool expire_token = false; // Whether or not to expire a token after torrent completion
	
	std::map<std::string, std::string>::const_iterator peer_id_iterator = params.find("peer_id");
	if(peer_id_iterator == params.end()) {
		return error("no peer id");
	}
	std::string peer_id = peer_id_iterator->second;
	peer_id = hex_decode(peer_id);
	
	if(whitelist.size() > 0) {
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
	}
	if(params["event"] == "completed") {
		completed_torrent = true;
	}
	
	peer * p;
	peer_list::iterator i;
	// Insert/find the peer in the torrent list
	if(left > 0 || completed_torrent) {
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
			i = tor.leechers.find(peer_id);
			if(i == tor.leechers.end()) {
				peer new_peer;
				std::pair<peer_list::iterator, bool> insert 
				= tor.seeders.insert(std::pair<std::string, peer>(peer_id, new_peer));
				
				p = &(insert.first->second);
				inserted = true;
			} else {
				p = &i->second;
				std::pair<peer_list::iterator, bool> insert
				= tor.seeders.insert(std::pair<std::string, peer>(peer_id, *p));
				tor.leechers.erase(peer_id);
				if(downloaded > 0) {
					std::cout << "Found unreported snatch from user " << u.id << " on torrent " << tor.id << std::endl;
				}
				p = &(insert.first->second);
//				completed_torrent = true; // Not sure if we want to do this. Might cause massive spam for broken clients (e.g. µTorrent 3)
			}
		} else {
			p = &i->second;
		}
		
		tor.last_seeded = cur_time;
	}
	
	// Update the peer
	p->left = left;
	int64_t upspeed = 0;
	int64_t downspeed = 0;
	int64_t real_uploaded_change = 0;
	int64_t real_downloaded_change = 0;
	
	if(inserted || params["event"] == "started") {
		//New peer on this torrent
		update_torrent = true;
		p->userid = u.id;
		p->peer_id = peer_id;
		p->first_announced = cur_time;
		p->last_announced = 0;
		p->uploaded = uploaded;
		p->downloaded = downloaded;
		p->corrupt = corrupt;
		p->announces = 1;
	} else if(uploaded < p->uploaded || downloaded < p->downloaded) {
		p->announces++;
		p->uploaded = uploaded;
		p->downloaded = downloaded;
	} else {
		int64_t uploaded_change = 0;
		int64_t downloaded_change = 0;
		int64_t corrupt_change = 0;
		p->announces++;
		
		if(uploaded != p->uploaded) {
			uploaded_change = uploaded - p->uploaded;
			real_uploaded_change = uploaded_change;
			p->uploaded = uploaded;
		}
		if(downloaded != p->downloaded) {
			downloaded_change = downloaded - p->downloaded;
			real_downloaded_change = downloaded_change;
			p->downloaded = downloaded;
		}
		if(corrupt != p->corrupt) {
			corrupt_change = corrupt - p->corrupt;
			p->corrupt = corrupt;
			tor.balance -= corrupt_change;
			update_torrent = true;
		}
		if(uploaded_change || downloaded_change) {
			tor.balance += uploaded_change;
			tor.balance -= downloaded_change;
			update_torrent = true;
			
			if(cur_time > p->last_announced) {
				upspeed = uploaded_change / (cur_time - p->last_announced);
				downspeed = downloaded_change / (cur_time - p->last_announced);
			}
			std::set<int>::iterator sit = tor.tokened_users.find(u.id);
			if (tor.free_torrent == NEUTRAL) {
				downloaded_change = 0;
				uploaded_change = 0;
			} else if(tor.free_torrent == FREE || sit != tor.tokened_users.end()) {
				if(sit != tor.tokened_users.end()) {
					expire_token = true;
					std::stringstream record;
					record << '(' << u.id << ',' << tor.id << ',' << downloaded_change << ')';
					std::string record_str = record.str();
					db->record_token(record_str);
				}
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
	} else if(completed_torrent) {
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
		if(expire_token) {
			(&s_comm)->expire_token(tor.id, u.id);
			tor.tokened_users.erase(u.id);
		}
		// do cache expire
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
					if(i == tor.seeders.end() || ++i == tor.seeders.end()) {
						i = tor.seeders.begin();
					}
				}
				
				// Find out where to end in the seeder list
				peer_list::const_iterator end;
				if(i == tor.seeders.begin()) {
					end = tor.seeders.end();
				} else {
					end = i;
					if(--end == tor.seeders.begin()) {
						end++;
						i++;
					}
				}
				
				// Add seeders
				while(i != end && found_peers < numwant) {
					if(i == tor.seeders.end()) {
						i = tor.seeders.begin();
					}
					if (i->second.userid == p->userid) //Don't add the peer to the peerlist if it's the same user
					{
						i++;
						continue;
					}
					peers.append(i->second.ip_port);
					found_peers++;
					tor.last_selected_seeder = i->second.peer_id;
					i++;
				}
			}

			if(found_peers < numwant && tor.leechers.size() > 1) {
				for(peer_list::const_iterator i = tor.leechers.begin(); i != tor.leechers.end() && found_peers < numwant; i++) {
					if(i->second.ip_port == p->ip_port || i->second.userid == p->userid) { // Don't show users themselves
						continue; 
					}
					found_peers++;
					peers.append(i->second.ip_port);
				}
				
			}
		} else if(tor.leechers.size() > 0) { // User is a seeder, and we have leechers!
			for(peer_list::const_iterator i = tor.leechers.begin(); i != tor.leechers.end() && found_peers < numwant; i++) {
				if(i->second.userid == p->userid) { // Don't show users themselves
						continue; 
				}
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
	record << '(' << u.id << ',' << tor.id << ',' << active << ',' << uploaded << ',' << downloaded << ',' << upspeed << ',' << downspeed << ',' << left << ',' << corrupt << ',' << (cur_time - p->first_announced) << ',' << p->announces << ',';
	std::string record_str = record.str();
	db->record_peer(record_str, ip, peer_id, headers["user-agent"]);

	std::string response = "d8:completei";
	response.reserve(350);
	response += inttostr(tor.seeders.size());
	response += "e10:downloadedi";
	response += inttostr(tor.completed);
	response += "e10:incompletei";
	response += inttostr(tor.leechers.size());
	response += "e8:intervali";
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
	response += "e";	
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
	if(params["action"] == "change_passkey") {
		std::string oldpasskey = params["oldpasskey"];
		std::string newpasskey = params["newpasskey"];
		user_list::iterator i = users_list.find(oldpasskey);
		if (i == users_list.end()) {
			std::cout << "No user with passkey " << oldpasskey << " exists when attempting to change passkey to " << newpasskey << std::endl;
		} else {
			users_list[newpasskey] = i->second;;
			users_list.erase(oldpasskey);
			std::cout << "changed passkey from " << oldpasskey << " to " << newpasskey << " for user " << i->second.id << std::endl;
		}
	} else if(params["action"] == "add_torrent") {
		torrent t;
		t.id = strtolong(params["id"]);
		std::string info_hash = params["info_hash"];
		info_hash = hex_decode(info_hash);
		if(params["freetorrent"] == "0") {
			t.free_torrent = NORMAL;
		} else if(params["freetorrent"] == "1") {
			t.free_torrent = FREE;
		} else {
			t.free_torrent = NEUTRAL;
		}
		t.balance = 0;
		t.completed = 0;
		t.last_selected_seeder = "";
		torrents_list[info_hash] = t;
		std::cout << "Added torrent " << t.id<< ". FL: " << t.free_torrent << " " << params["freetorrent"] << std::endl;
	} else if(params["action"] == "update_torrent") {
		std::string info_hash = params["info_hash"];
		info_hash = hex_decode(info_hash);
		freetype fl;
		if(params["freetorrent"] == "0") {
			fl = NORMAL;
		} else if(params["freetorrent"] == "1") {
			fl = FREE;
		} else {
			fl = NEUTRAL;
		}
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			torrent_it->second.free_torrent = fl;
			std::cout << "Updated torrent " << torrent_it->second.id << " to FL " << fl << std::endl;
		} else {
			std::cout << "Failed to find torrent " << info_hash << " to FL " << fl << std::endl;
		}
	} else if(params["action"] == "update_torrents") {
		// Each decoded infohash is exactly 20 characters long.
		std::string info_hashes = params["info_hashes"];
		info_hashes = hex_decode(info_hashes);
		freetype fl;
		if(params["freetorrent"] == "0") {
			fl = NORMAL;
		} else if(params["freetorrent"] == "1") {
			fl = FREE;
		} else {
			fl = NEUTRAL;
		}
		for(unsigned int pos = 0; pos < info_hashes.length(); pos += 20) {
			std::string info_hash = info_hashes.substr(pos, 20);
			auto torrent_it = torrents_list.find(info_hash);
			if (torrent_it != torrents_list.end()) {
				torrent_it->second.free_torrent = fl;
				std::cout << "Updated torrent " << torrent_it->second.id << " to FL " << fl << std::endl;
			} else {
				std::cout << "Failed to find torrent " << info_hash << " to FL " << fl << std::endl;
			}
		}
	} else if(params["action"] == "add_token") {
		std::string info_hash = hex_decode(params["info_hash"]);
		int user_id = atoi(params["userid"].c_str());
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			torrent_it->second.tokened_users.insert(user_id);
		} else {
			std::cout << "Failed to find torrent to add a token for user " << user_id << std::endl;
		}
	} else if(params["action"] == "remove_token") {
		std::string info_hash = hex_decode(params["info_hash"]);
		int user_id = atoi(params["userid"].c_str());
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			torrent_it->second.tokened_users.erase(user_id);
		} else {
			std::cout << "Failed to find torrent " << info_hash << " to remove token for user " << user_id << std::endl;
		}
	} else if(params["action"] == "delete_torrent") {
		std::string info_hash = params["info_hash"];
		info_hash = hex_decode(info_hash);
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			std::cout << "Deleting torrent " << torrent_it->second.id << std::endl;
			torrents_list.erase(torrent_it);
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
		user_list::iterator i = users_list.find(passkey);
		if (i == users_list.end()) {
			std::cout << "No user with passkey " << passkey << " found when attempting to change leeching status!" << std::endl;
		} else {
			users_list[passkey].can_leech = can_leech;
			std::cout << "Updated user " << passkey << std::endl;
		}
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
	} else if(params["action"] == "info_torrent") {
		std::string info_hash_hex = params["info_hash"];
		std::string info_hash = hex_decode(info_hash_hex);
		std::cout << "Info for torrent '" << info_hash_hex << "'" << std::endl;
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			std::cout << "Torrent " << torrent_it->second.id
				<< ", freetorrent = " << torrent_it->second.free_torrent << std::endl;
		} else {
			std::cout << "Failed to find torrent " << info_hash_hex << std::endl;
		}
	}
	return "success";
}

void worker::reap_peers() {
	std::cout << "started reaper" << std::endl;
	boost::thread thread(&worker::do_reap_peers, this);
}

void worker::do_reap_peers() {
	db->logger_ptr->log("Began worker::do_reap_peers()");
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
	db->logger_ptr->log("Completed worker::do_reap_peers()");
}
