#include <iostream>
#include <string>
#include <map>
#include <sstream>
#include <list>
#include <vector>
#include <set>
#include <algorithm>
#include <mutex>
#include <thread>

#include "ocelot.h"
#include "config.h"
#include "db.h"
#include "worker.h"
#include "misc_functions.h"
#include "site_comm.h"
#include "response.h"
#include "report.h"
#include "user.h"

//---------- Worker - does stuff with input
worker::worker(config * conf_obj, torrent_list &torrents, user_list &users, std::vector<std::string> &_whitelist, mysql * db_obj, site_comm * sc) :
	conf(conf_obj), db(db_obj), s_comm(sc), torrents_list(torrents), users_list(users), whitelist(_whitelist), status(OPEN), reaper_active(false)
{
	load_config(conf);
}

void worker::load_config(config * conf) {
	announce_interval = conf->get_uint("announce_interval");
	del_reason_lifetime = conf->get_uint("del_reason_lifetime");
	peers_timeout = conf->get_uint("peers_timeout");
	numwant_limit = conf->get_uint("numwant_limit");
	keepalive_enabled = conf->get_uint("keepalive_timeout") != 0;
	site_password = conf->get_str("site_password");
	report_password = conf->get_str("report_password");
}

void worker::reload_config(config * conf) {
	load_config(conf);
}

void worker::reload_lists() {
	status = PAUSED;
	db->load_torrents(torrents_list);
	db->load_users(users_list);
	db->load_whitelist(whitelist);
	status = OPEN;
}

bool worker::shutdown() {
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

std::string worker::work(const std::string &input, std::string &ip, client_opts_t &client_opts) {
	unsigned int input_length = input.length();

	//---------- Parse request - ugly but fast. Using substr exploded.
	if (input_length < 60) { // Way too short to be anything useful
		return error("GET string too short", client_opts);
	}

	size_t pos = 5; // skip 'GET /'

	// Get the passkey
	std::string passkey;
	passkey.reserve(32);
	if (input[37] != '/') {
		return error("Malformed announce", client_opts);
	}

	for (; pos < 37; pos++) {
		passkey.push_back(input[pos]);
	}

	pos = 38;

	// Get the action
	enum action_t {
		INVALID = 0, ANNOUNCE, SCRAPE, UPDATE, REPORT
	};
	action_t action = INVALID;

	switch (input[pos]) {
		case 'a':
			stats.announcements++;
			action = ANNOUNCE;
			pos += 8;
			break;
		case 's':
			stats.scrapes++;
			action = SCRAPE;
			pos += 6;
			break;
		case 'u':
			action = UPDATE;
			pos += 6;
			break;
		case 'r':
			action = REPORT;
			pos += 6;
			break;
	}

	if (input[pos] != '?') {
		// No parameters given. Probably means we're not talking to a torrent client
		client_opts.html = true;
		return response("Nothing to see here", client_opts);
	}

	// Parse URL params
	std::list<std::string> infohashes; // For scrape only

	params_type params;
	std::string key;
	std::string value;
	bool parsing_key = true; // true = key, false = value

	++pos; // Skip the '?'
	for (; pos < input_length; ++pos) {
		if (input[pos] == '=') {
			parsing_key = false;
		} else if (input[pos] == '&' || input[pos] == ' ') {
			parsing_key = true;
			if (action == SCRAPE && key == "info_hash") {
				infohashes.push_back(value);
			} else {
				params[key] = value;
			}
			key.clear();
			value.clear();
			if (input[pos] == ' ') {
				break;
			}
		} else {
			if (parsing_key) {
				key.push_back(input[pos]);
			} else {
				value.push_back(input[pos]);
			}
		}
	}
	++pos;

	if (input.compare(pos, 5, "HTTP/") != 0) {
		return error("Malformed HTTP request", client_opts);
	}

	std::string http_version;
	pos += 5;
	while (input[pos] != '\r' && input[pos] != '\n') {
		http_version.push_back(input[pos]);
		++pos;
	}
	++pos; // skip line break - should probably be += 2, but just in case a client doesn't send \r

	// Parse headers
	params_type headers;
	parsing_key = true;
	bool found_data = false;

	for (; pos < input_length; ++pos) {
		if (input[pos] == ':') {
			parsing_key = false;
			++pos; // skip space after :
		} else if (input[pos] == '\n' || input[pos] == '\r') {
			parsing_key = true;

			if (found_data) {
				found_data = false; // dodge for getting around \r\n or just \n
				std::transform(key.begin(), key.end(), key.begin(), ::tolower);
				headers[key] = value;
				key.clear();
				value.clear();
			}
		} else {
			found_data = true;
			if (parsing_key) {
				key.push_back(input[pos]);
			} else {
				value.push_back(input[pos]);
			}
		}
	}

	if (keepalive_enabled) {
		auto hdr_http_close = headers.find("connection");
		if (hdr_http_close == headers.end()) {
			client_opts.http_close = (http_version == "1.0");
		} else {
			client_opts.http_close = (hdr_http_close->second != "Keep-Alive");
		}
	} else {
		client_opts.http_close = true;
	}

	if (status != OPEN) {
		return error("The tracker is temporarily unavailable.", client_opts);
	}

	if (action == INVALID) {
		return error("Invalid action", client_opts);
	}

	if (action == UPDATE) {
		if (passkey == site_password) {
			return update(params, client_opts);
		} else {
			return error("Authentication failure", client_opts);
		}
	}

	if (action == REPORT) {
		if (passkey == report_password) {
			std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
			return report(params, users_list, client_opts);
		} else {
			return error("Authentication failure", client_opts);
		}
	}

	// Either a scrape or an announce

	std::unique_lock<std::mutex> ul_lock(db->user_list_mutex);
	auto user_it = users_list.find(passkey);
	if (user_it == users_list.end()) {
		return error("Passkey not found", client_opts);
	}
	user_ptr u = user_it->second;
	ul_lock.unlock();

	if (action == ANNOUNCE) {
		// Let's translate the infohash into something nice
		// info_hash is a url encoded (hex) base 20 number
		std::string info_hash_decoded = hex_decode(params["info_hash"]);
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto tor = torrents_list.find(info_hash_decoded);
		if (tor == torrents_list.end()) {
			std::lock_guard<std::mutex> dr_lock(del_reasons_lock);
			auto msg = del_reasons.find(info_hash_decoded);
			if (msg != del_reasons.end()) {
				if (msg->second.reason != -1) {
					return error("Unregistered torrent: " + get_del_reason(msg->second.reason), client_opts);
				} else {
					return error("Unregistered torrent", client_opts);
				}
			} else {
				return error("Unregistered torrent", client_opts);
			}
		}
		return announce(input, tor->second, u, params, headers, ip, client_opts);
	} else {
		return scrape(infohashes, headers, client_opts);
	}
}

std::string worker::announce(const std::string &input, torrent &tor, user_ptr &u, params_type &params, params_type &headers, std::string &ip, client_opts_t &client_opts) {
	cur_time = time(NULL);

	if (params["compact"] != "1") {
		return error("Your client does not support compact announces", client_opts);
	}

	int64_t left = std::max((int64_t)0, strtoint64(params["left"]));
	int64_t uploaded = std::max((int64_t)0, strtoint64(params["uploaded"]));
	int64_t downloaded = std::max((int64_t)0, strtoint64(params["downloaded"]));
	int64_t corrupt = std::max((int64_t)0, strtoint64(params["corrupt"]));

	int snatched = 0; // This is the value that gets sent to the database on a snatch
	int active = 1; // This is the value that marks a peer as active/inactive in the database
	bool inserted = false; // If we insert the peer as opposed to update
	bool update_torrent = false; // Whether or not we should update the torrent in the DB
	bool completed_torrent = false; // Whether or not the current announcement is a snatch
	bool stopped_torrent = false; // Was the torrent just stopped?
	bool expire_token = false; // Whether or not to expire a token after torrent completion
	bool peer_changed = false; // Whether or not the peer is new or has changed since the last announcement
	bool invalid_ip = false;
	bool inc_l = false, inc_s = false, dec_l = false, dec_s = false;
	userid_t userid = u->get_id();

	params_type::const_iterator peer_id_iterator = params.find("peer_id");
	if (peer_id_iterator == params.end()) {
		return error("No peer ID", client_opts);
	}
	const std::string peer_id = hex_decode(peer_id_iterator->second);
	if (peer_id.length() != 20) {
		return error("Invalid peer ID", client_opts);
	}

	std::unique_lock<std::mutex> wl_lock(db->whitelist_mutex);
	if (whitelist.size() > 0) {
		bool found = false; // Found client in whitelist?
		for (unsigned int i = 0; i < whitelist.size(); i++) {
			if (peer_id.compare(0, whitelist[i].length(), whitelist[i]) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			return error("Your client is not on the whitelist", client_opts);
		}
	}
	wl_lock.unlock();

	std::stringstream peer_key_stream;
	peer_key_stream << peer_id[12 + (tor.id & 7)] // "Randomize" the element order in the peer map by prefixing with a peer id byte
		<< userid // Include user id in the key to lower chance of peer id collisions
		<< peer_id;
	const std::string peer_key(peer_key_stream.str());

	if (params["event"] == "completed") {
		// Don't update <snatched> here as we may decide to use other conditions later on
		completed_torrent = (left == 0); // Sanity check just to be extra safe
	} else if (params["event"] == "stopped") {
		stopped_torrent = true;
		peer_changed = true;
		update_torrent = true;
		active = 0;
	}
	peer * p;
	peer_list::iterator peer_it;
	// Insert/find the peer in the torrent list
	if (left > 0) {
		peer_it = tor.leechers.find(peer_key);
		if (peer_it == tor.leechers.end()) {
			// We could search the seed list as well, but the peer reaper will sort things out eventually
			peer_it = add_peer(tor.leechers, peer_key);
			inserted = true;
			inc_l = true;
		}
	} else if (completed_torrent) {
		peer_it = tor.leechers.find(peer_key);
		if (peer_it == tor.leechers.end()) {
			peer_it = tor.seeders.find(peer_key);
			if (peer_it == tor.seeders.end()) {
				peer_it = add_peer(tor.seeders, peer_key);
				inserted = true;
				inc_s = true;
			} else {
				completed_torrent = false;
			}
		} else if (tor.seeders.find(peer_key) != tor.seeders.end()) {
			// If the peer exists in both peer lists, just decrement the seed count.
			// Should be cheaper than searching the seed list in the left > 0 case
			dec_s = true;
		}
	} else {
		peer_it = tor.seeders.find(peer_key);
		if (peer_it == tor.seeders.end()) {
			peer_it = tor.leechers.find(peer_key);
			if (peer_it == tor.leechers.end()) {
				peer_it = add_peer(tor.seeders, peer_key);
				inserted = true;
			} else {
				p = &peer_it->second;
				std::pair<peer_list::iterator, bool> insert
				= tor.seeders.insert(std::pair<std::string, peer>(peer_key, *p));
				tor.leechers.erase(peer_it);
				peer_it = insert.first;
				peer_changed = true;
				dec_l = true;
			}
			inc_s = true;
		}
	}
	p = &peer_it->second;

	int64_t upspeed = 0;
	int64_t downspeed = 0;
	if (inserted || params["event"] == "started") {
		// New peer on this torrent (maybe)
		update_torrent = true;
		if (inserted) {
			// If this was an existing peer, the user pointer will be corrected later
			p->user = u;
		}
		p->first_announced = cur_time;
		p->last_announced = 0;
		p->uploaded = uploaded;
		p->downloaded = downloaded;
		p->corrupt = corrupt;
		p->announces = 1;
		peer_changed = true;
	} else if (uploaded < p->uploaded || downloaded < p->downloaded) {
		p->announces++;
		p->uploaded = uploaded;
		p->downloaded = downloaded;
		peer_changed = true;
	} else {
		int64_t uploaded_change = 0;
		int64_t downloaded_change = 0;
		int64_t corrupt_change = 0;
		p->announces++;

		if (uploaded != p->uploaded) {
			uploaded_change = uploaded - p->uploaded;
			p->uploaded = uploaded;
		}
		if (downloaded != p->downloaded) {
			downloaded_change = downloaded - p->downloaded;
			p->downloaded = downloaded;
		}
		if (corrupt != p->corrupt) {
			corrupt_change = corrupt - p->corrupt;
			p->corrupt = corrupt;
			tor.balance -= corrupt_change;
			update_torrent = true;
		}
		peer_changed = peer_changed || uploaded_change || downloaded_change || corrupt_change;

		if (uploaded_change || downloaded_change) {
			tor.balance += uploaded_change;
			tor.balance -= downloaded_change;
			update_torrent = true;

			if (cur_time > p->last_announced) {
				upspeed = uploaded_change / (cur_time - p->last_announced);
				downspeed = downloaded_change / (cur_time - p->last_announced);
			}
			auto sit = tor.tokened_users.find(userid);
			if (tor.free_torrent == NEUTRAL) {
				downloaded_change = 0;
				uploaded_change = 0;
			} else if (tor.free_torrent == FREE || sit != tor.tokened_users.end()) {
				if (sit != tor.tokened_users.end()) {
					expire_token = true;
					std::stringstream record;
					record << '(' << userid << ',' << tor.id << ',' << downloaded_change << ')';
					std::string record_str = record.str();
					db->record_token(record_str);
				}
				downloaded_change = 0;
			}

			if (uploaded_change || downloaded_change) {
				std::stringstream record;
				record << '(' << userid << ',' << uploaded_change << ',' << downloaded_change << ')';
				std::string record_str = record.str();
				db->record_user(record_str);
			}
		}
	}
	p->left = left;

	params_type::const_iterator param_ip = params.find("ip");
	if (param_ip != params.end()) {
		ip = param_ip->second;
	} else if ((param_ip = params.find("ipv4")) != params.end()) {
		ip = param_ip->second;
	} else {
		auto head_itr = headers.find("x-forwarded-for");
		if (head_itr != headers.end()) {
			size_t ip_end_pos = head_itr->second.find(',');
			if (ip_end_pos != std::string::npos) {
				ip = head_itr->second.substr(0, ip_end_pos);
			} else {
				ip = head_itr->second;
			}
		}
	}

	uint16_t port = strtoint32(params["port"]) & 0xFFFF;
	// Generate compact ip/port string
	if (inserted || port != p->port || ip != p->ip) {
		p->port = port;
		p->ip = ip;
		p->ip_port = "";
		char x = 0;
		for (size_t pos = 0, end = ip.length(); pos < end; pos++) {
			if (ip[pos] == '.') {
				p->ip_port.push_back(x);
				x = 0;
				continue;
			} else if (!isdigit(ip[pos])) {
				invalid_ip = true;
				break;
			}
			x = x * 10 + ip[pos] - '0';
		}
		if (!invalid_ip) {
			p->ip_port.push_back(x);
			p->ip_port.push_back(port >> 8);
			p->ip_port.push_back(port & 0xFF);
		}
		if (p->ip_port.length() != 6) {
			p->ip_port.clear();
			invalid_ip = true;
		}
		p->invalid_ip = invalid_ip;
	} else {
		invalid_ip = p->invalid_ip;
	}

	// Update the peer
	p->last_announced = cur_time;
	p->visible = peer_is_visible(u, p);

	// Add peer data to the database
	std::stringstream record;
	if (peer_changed) {
		record << '(' << userid << ',' << tor.id << ',' << active << ',' << uploaded << ',' << downloaded << ',' << upspeed << ',' << downspeed << ',' << left << ',' << corrupt << ',' << (cur_time - p->first_announced) << ',' << p->announces << ',';
		std::string record_str = record.str();
		std::string record_ip;
		if (u->is_protected()) {
			record_ip = "";
		} else {
			record_ip = ip;
		}
		db->record_peer(record_str, record_ip, peer_id, headers["user-agent"]);
	} else {
		record << '(' << userid << ',' << tor.id << ',' << (cur_time - p->first_announced) << ',' << p->announces << ',';
		std::string record_str = record.str();
		db->record_peer(record_str, peer_id);
	}

	// Select peers!
	uint32_t numwant;
	params_type::const_iterator param_numwant = params.find("numwant");
	if (param_numwant == params.end()) {
		numwant = numwant_limit;
	} else {
		numwant = std::min((int32_t)numwant_limit, strtoint32(param_numwant->second));
	}

	if (stopped_torrent) {
		numwant = 0;
		if (left > 0) {
			dec_l = true;
		} else {
			dec_s = true;
		}
	} else if (completed_torrent) {
		snatched = 1;
		update_torrent = true;
		tor.completed++;

		std::stringstream record;
		std::string record_ip;
		if (u->is_protected()) {
			record_ip = "";
		} else {
			record_ip = ip;
		}
		record << '(' << userid << ',' << tor.id << ',' << cur_time;
		std::string record_str = record.str();
		db->record_snatch(record_str, record_ip);

		// User is a seeder now!
		if (!inserted) {
			std::pair<peer_list::iterator, bool> insert
			= tor.seeders.insert(std::pair<std::string, peer>(peer_key, *p));
			tor.leechers.erase(peer_it);
			peer_it = insert.first;
			p = &peer_it->second;
			dec_l = inc_s = true;
		}
		if (expire_token) {
			s_comm->expire_token(tor.id, userid);
			tor.tokened_users.erase(userid);
		}
	} else if (!u->can_leech() && left > 0) {
		numwant = 0;
	}

	std::string peers;
	if (numwant > 0) {
		peers.reserve(numwant*6);
		unsigned int found_peers = 0;
		if (left > 0) { // Show seeders to leechers first
			if (tor.seeders.size() > 0) {
				// We do this complicated stuff to cycle through the seeder list, so all seeders will get shown to leechers

				// Find out where to begin in the seeder list
				peer_list::const_iterator i;
				if (tor.last_selected_seeder == "") {
					i = tor.seeders.begin();
				} else {
					i = tor.seeders.find(tor.last_selected_seeder);
					if (i == tor.seeders.end() || ++i == tor.seeders.end()) {
						i = tor.seeders.begin();
					}
				}

				// Find out where to end in the seeder list
				peer_list::const_iterator end;
				if (i == tor.seeders.begin()) {
					end = tor.seeders.end();
				} else {
					end = i;
					if (--end == tor.seeders.begin()) {
						++end;
						++i;
					}
				}

				// Add seeders
				while (i != end && found_peers < numwant) {
					if (i == tor.seeders.end()) {
						i = tor.seeders.begin();
					}
					// Don't show users themselves
					if (i->second.user->is_deleted() || i->second.user->get_id() == userid || !i->second.visible) {
						++i;
						continue;
					}
					peers.append(i->second.ip_port);
					found_peers++;
					tor.last_selected_seeder = i->first;
					++i;
				}
			}

			if (found_peers < numwant && tor.leechers.size() > 1) {
				for (peer_list::const_iterator i = tor.leechers.begin(); i != tor.leechers.end() && found_peers < numwant; ++i) {
					// Don't show users themselves or leech disabled users
					if (i->second.user->is_deleted() || i->second.ip_port == p->ip_port || i->second.user->get_id() == userid || !i->second.visible) {
						continue;
					}
					found_peers++;
					peers.append(i->second.ip_port);
				}

			}
		} else if (tor.leechers.size() > 0) { // User is a seeder, and we have leechers!
			for (peer_list::const_iterator i = tor.leechers.begin(); i != tor.leechers.end() && found_peers < numwant; ++i) {
				// Don't show users themselves or leech disabled users
				if (i->second.user->get_id() == userid || !i->second.visible) {
					continue;
				}
				found_peers++;
				peers.append(i->second.ip_port);
			}
		}
	}

	// Update the stats
	stats.succ_announcements++;
	if (dec_l || dec_s || inc_l || inc_s) {
		if (inc_l) {
			p->user->incr_leeching();
			stats.leechers++;
		}
		if (inc_s) {
			p->user->incr_seeding();
			stats.seeders++;
		}
		if (dec_l) {
			p->user->decr_leeching();
			stats.leechers--;
		}
		if (dec_s) {
			p->user->decr_seeding();
			stats.seeders--;
		}
	}

	// Correct the stats for the old user if the peer's user link has changed
	if (p->user != u) {
		if (!stopped_torrent) {
			if (left > 0) {
				u->incr_leeching();
				p->user->decr_leeching();
			} else {
				u->incr_seeding();
				p->user->decr_seeding();
			}
		}
		p->user = u;
	}

	// Delete peers as late as possible to prevent access problems
	if (stopped_torrent) {
		if (left > 0) {
			tor.leechers.erase(peer_it);
		} else {
			tor.seeders.erase(peer_it);
		}
	}

	// Putting this after the peer deletion gives us accurate swarm sizes
	if (update_torrent || tor.last_flushed + 3600 < cur_time) {
		tor.last_flushed = cur_time;

		std::stringstream record;
		record << '(' << tor.id << ',' << tor.seeders.size() << ',' << tor.leechers.size() << ',' << snatched << ',' << tor.balance << ')';
		std::string record_str = record.str();
		db->record_torrent(record_str);
	}

	if (!u->can_leech() && left > 0) {
		return error("Access denied, leeching forbidden", client_opts);
	}

	std::string output = "d8:completei";
	output.reserve(350);
	output += inttostr(tor.seeders.size());
	output += "e10:downloadedi";
	output += inttostr(tor.completed);
	output += "e10:incompletei";
	output += inttostr(tor.leechers.size());
	output += "e8:intervali";
	output += inttostr(announce_interval + std::min((size_t)600, tor.seeders.size())); // ensure a more even distribution of announces/second
	output += "e12:min intervali";
	output += inttostr(announce_interval);
	output += "e5:peers";
	if (peers.length() == 0) {
		output += "0:";
	} else {
		output += inttostr(peers.length());
		output += ":";
		output += peers;
	}
	if (invalid_ip) {
		output += warning("Illegal character found in IP address. IPv6 is not supported");
	}
	output += 'e';

	/* gzip compression actually makes announce returns larger from our
	 * testing. Feel free to enable this here if you'd like but be aware of
	 * possibly inflated return size
	 */
	/*if (headers["accept-encoding"].find("gzip") != std::string::npos) {
		client_opts.gzip = true;
	}*/
	return response(output, client_opts);
}

std::string worker::scrape(const std::list<std::string> &infohashes, params_type &headers, client_opts_t &client_opts) {
	std::string output = "d5:filesd";
	for (std::list<std::string>::const_iterator i = infohashes.begin(); i != infohashes.end(); ++i) {
		std::string infohash = *i;
		infohash = hex_decode(infohash);

		torrent_list::iterator tor = torrents_list.find(infohash);
		if (tor == torrents_list.end()) {
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
	output += "ee";
	if (headers["accept-encoding"].find("gzip") != std::string::npos) {
		client_opts.gzip = true;
	}
	return response(output, client_opts);
}

//TODO: Restrict to local IPs
std::string worker::update(params_type &params, client_opts_t &client_opts) {
	if (params["action"] == "change_passkey") {
		std::string oldpasskey = params["oldpasskey"];
		std::string newpasskey = params["newpasskey"];
		std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
		auto u = users_list.find(oldpasskey);
		if (u == users_list.end()) {
			std::cout << "No user with passkey " << oldpasskey << " exists when attempting to change passkey to " << newpasskey << std::endl;
		} else {
			users_list[newpasskey] = u->second;
			users_list.erase(oldpasskey);
			std::cout << "Changed passkey from " << oldpasskey << " to " << newpasskey << " for user " << u->second->get_id() << std::endl;
		}
	} else if (params["action"] == "add_torrent") {
		torrent *t;
		std::string info_hash = params["info_hash"];
		info_hash = hex_decode(info_hash);
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto i = torrents_list.find(info_hash);
		if (i == torrents_list.end()) {
			t = &torrents_list[info_hash];
			t->id = strtoint32(params["id"]);
			t->balance = 0;
			t->completed = 0;
			t->last_selected_seeder = "";
		} else {
			t = &i->second;
		}
		if (params["freetorrent"] == "0") {
			t->free_torrent = NORMAL;
		} else if (params["freetorrent"] == "1") {
			t->free_torrent = FREE;
		} else {
			t->free_torrent = NEUTRAL;
		}
		std::cout << "Added torrent " << t->id << ". FL: " << t->free_torrent << " " << params["freetorrent"] << std::endl;
	} else if (params["action"] == "update_torrent") {
		std::string info_hash = params["info_hash"];
		info_hash = hex_decode(info_hash);
		freetype fl;
		if (params["freetorrent"] == "0") {
			fl = NORMAL;
		} else if (params["freetorrent"] == "1") {
			fl = FREE;
		} else {
			fl = NEUTRAL;
		}
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			torrent_it->second.free_torrent = fl;
			std::cout << "Updated torrent " << torrent_it->second.id << " to FL " << fl << std::endl;
		} else {
			std::cout << "Failed to find torrent " << info_hash << " to FL " << fl << std::endl;
		}
	} else if (params["action"] == "update_torrents") {
		// Each decoded infohash is exactly 20 characters long.
		std::string info_hashes = params["info_hashes"];
		info_hashes = hex_decode(info_hashes);
		freetype fl;
		if (params["freetorrent"] == "0") {
			fl = NORMAL;
		} else if (params["freetorrent"] == "1") {
			fl = FREE;
		} else {
			fl = NEUTRAL;
		}
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		for (unsigned int pos = 0; pos < info_hashes.length(); pos += 20) {
			std::string info_hash = info_hashes.substr(pos, 20);
			auto torrent_it = torrents_list.find(info_hash);
			if (torrent_it != torrents_list.end()) {
				torrent_it->second.free_torrent = fl;
				std::cout << "Updated torrent " << torrent_it->second.id << " to FL " << fl << std::endl;
			} else {
				std::cout << "Failed to find torrent " << info_hash << " to FL " << fl << std::endl;
			}
		}
	} else if (params["action"] == "add_token") {
		std::string info_hash = hex_decode(params["info_hash"]);
		int userid = atoi(params["userid"].c_str());
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			torrent_it->second.tokened_users.insert(userid);
		} else {
			std::cout << "Failed to find torrent to add a token for user " << userid << std::endl;
		}
	} else if (params["action"] == "remove_token") {
		std::string info_hash = hex_decode(params["info_hash"]);
		int userid = atoi(params["userid"].c_str());
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			torrent_it->second.tokened_users.erase(userid);
		} else {
			std::cout << "Failed to find torrent " << info_hash << " to remove token for user " << userid << std::endl;
		}
	} else if (params["action"] == "delete_torrent") {
		std::string info_hash = params["info_hash"];
		info_hash = hex_decode(info_hash);
		int reason = -1;
		auto reason_it = params.find("reason");
		if (reason_it != params.end()) {
			reason = atoi(params["reason"].c_str());
		}
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			std::cout << "Deleting torrent " << torrent_it->second.id << " for the reason '" << get_del_reason(reason) << "'" << std::endl;
			stats.leechers -= torrent_it->second.leechers.size();
			stats.seeders -= torrent_it->second.seeders.size();
			for (auto &p: torrent_it->second.leechers) {
				p.second.user->decr_leeching();
			}
			for (auto &p: torrent_it->second.seeders) {
				p.second.user->decr_seeding();
			}
			std::lock_guard<std::mutex> dr_lock(del_reasons_lock);
			del_message msg;
			msg.reason = reason;
			msg.time = time(NULL);
			del_reasons[info_hash] = msg;
			torrents_list.erase(torrent_it);
		} else {
			std::cout << "Failed to find torrent " << bintohex(info_hash) << " to delete " << std::endl;
		}
	} else if (params["action"] == "add_user") {
		std::string passkey = params["passkey"];
		userid_t userid = strtoint32(params["id"]);
		std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
		auto u = users_list.find(passkey);
		if (u == users_list.end()) {
			bool protect_ip = params["visible"] == "0";
			user_ptr tmp_user = std::make_shared<user>(userid, true, protect_ip);
			users_list.insert(std::pair<std::string, user_ptr>(passkey, tmp_user));
			std::cout << "Added user " << passkey << " with id " << userid << std::endl;
		} else {
			std::cout << "Tried to add already known user " << passkey << " with id " << userid << std::endl;
			u->second->set_deleted(false);
		}
	} else if (params["action"] == "remove_user") {
		std::string passkey = params["passkey"];
		std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
		auto u = users_list.find(passkey);
		if (u != users_list.end()) {
			std::cout << "Removed user " << passkey << " with id " << u->second->get_id() << std::endl;
			u->second->set_deleted(true);
			users_list.erase(u);
		}
	} else if (params["action"] == "remove_users") {
		// Each passkey is exactly 32 characters long.
		std::string passkeys = params["passkeys"];
		std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
		for (unsigned int pos = 0; pos < passkeys.length(); pos += 32) {
			std::string passkey = passkeys.substr(pos, 32);
			auto u = users_list.find(passkey);
			if (u != users_list.end()) {
				std::cout << "Removed user " << passkey << std::endl;
				u->second->set_deleted(true);
				users_list.erase(passkey);
			}
		}
	} else if (params["action"] == "update_user") {
		std::string passkey = params["passkey"];
		bool can_leech = true;
		bool protect_ip = false;
		if (params["can_leech"] == "0") {
			can_leech = false;
		}
		if (params["visible"] == "0") {
			protect_ip = true;
		}
		std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
		user_list::iterator i = users_list.find(passkey);
		if (i == users_list.end()) {
			std::cout << "No user with passkey " << passkey << " found when attempting to change leeching status!" << std::endl;
		} else {
			i->second->set_protected(protect_ip);
			i->second->set_leechstatus(can_leech);
			std::cout << "Updated user " << passkey << std::endl;
		}
	} else if (params["action"] == "add_whitelist") {
		std::string peer_id = params["peer_id"];
		std::lock_guard<std::mutex> wl_lock(db->whitelist_mutex);
		whitelist.push_back(peer_id);
		std::cout << "Whitelisted " << peer_id << std::endl;
	} else if (params["action"] == "remove_whitelist") {
		std::string peer_id = params["peer_id"];
		std::lock_guard<std::mutex> wl_lock(db->whitelist_mutex);
		for (unsigned int i = 0; i < whitelist.size(); i++) {
			if (whitelist[i].compare(peer_id) == 0) {
				whitelist.erase(whitelist.begin() + i);
				break;
			}
		}
		std::cout << "De-whitelisted " << peer_id << std::endl;
	} else if (params["action"] == "edit_whitelist") {
		std::string new_peer_id = params["new_peer_id"];
		std::string old_peer_id = params["old_peer_id"];
		std::lock_guard<std::mutex> wl_lock(db->whitelist_mutex);
		for (unsigned int i = 0; i < whitelist.size(); i++) {
			if (whitelist[i].compare(old_peer_id) == 0) {
				whitelist.erase(whitelist.begin() + i);
				break;
			}
		}
		whitelist.push_back(new_peer_id);
		std::cout << "Edited whitelist item from " << old_peer_id << " to " << new_peer_id << std::endl;
	} else if (params["action"] == "update_announce_interval") {
		const std::string interval = params["new_announce_interval"];
		conf->set("announce_interval", interval);
		announce_interval = conf->get_uint("announce_interval");
		std::cout << "Edited announce interval to " << announce_interval << std::endl;
	} else if (params["action"] == "info_torrent") {
		std::string info_hash_hex = params["info_hash"];
		std::string info_hash = hex_decode(info_hash_hex);
		std::cout << "Info for torrent '" << info_hash_hex << "'" << std::endl;
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			std::cout << "Torrent " << torrent_it->second.id
				<< ", freetorrent = " << torrent_it->second.free_torrent << std::endl;
		} else {
			std::cout << "Failed to find torrent " << info_hash_hex << std::endl;
		}
	}
	return response("success", client_opts);
}

peer_list::iterator worker::add_peer(peer_list &peer_list, const std::string &peer_key) {
	peer new_peer;
	auto it = peer_list.insert(std::pair<std::string, peer>(peer_key, new_peer));
	return it.first;
}

void worker::start_reaper() {
	if (!reaper_active) {
		std::thread thread(&worker::do_start_reaper, this);
		thread.detach();
	}
}

void worker::do_start_reaper() {
	reaper_active = true;
	reap_peers();
	reap_del_reasons();
	reaper_active = false;
}

void worker::reap_peers() {
	std::cout << "Starting peer reaper" << std::endl;
	cur_time = time(NULL);
	unsigned int reaped_l = 0, reaped_s = 0;
	unsigned int cleared_torrents = 0;
	for (auto t = torrents_list.begin(); t != torrents_list.end(); ++t) {
		bool reaped_this = false; // True if at least one peer was deleted from the current torrent
		auto p = t->second.leechers.begin();
		peer_list::iterator del_p;
		while (p != t->second.leechers.end()) {
			if (p->second.last_announced + peers_timeout < cur_time) {
				std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
				del_p = p++;
				del_p->second.user->decr_leeching();
				t->second.leechers.erase(del_p);
				reaped_this = true;
				reaped_l++;
			} else {
				++p;
			}
		}
		p = t->second.seeders.begin();
		while (p != t->second.seeders.end()) {
			if (p->second.last_announced + peers_timeout < cur_time) {
				std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
				del_p = p++;
				del_p->second.user->decr_seeding();
				t->second.seeders.erase(del_p);
				reaped_this = true;
				reaped_s++;
			} else {
				++p;
			}
		}
		if (reaped_this && t->second.seeders.empty() && t->second.leechers.empty()) {
			std::stringstream record;
			record << '(' << t->second.id << ",0,0,0," << t->second.balance << ')';
			std::string record_str = record.str();
			db->record_torrent(record_str);
			cleared_torrents++;
		}
	}
	if (reaped_l || reaped_s) {
		stats.leechers -= reaped_l;
		stats.seeders -= reaped_s;
	}
	std::cout << "Reaped " << reaped_l << " leechers and " << reaped_s << " seeders. Reset " << cleared_torrents << " torrents" << std::endl;
}

void worker::reap_del_reasons()
{
	std::cout << "Starting del reason reaper" << std::endl;
	time_t max_time = time(NULL) - del_reason_lifetime;
	auto it = del_reasons.begin();
	unsigned int reaped = 0;
	for (; it != del_reasons.end(); ) {
		if (it->second.time <= max_time) {
			auto del_it = it++;
			std::lock_guard<std::mutex> dr_lock(del_reasons_lock);
			del_reasons.erase(del_it);
			reaped++;
			continue;
		}
		++it;
	}
	std::cout << "Reaped " << reaped << " del reasons" << std::endl;
}

std::string worker::get_del_reason(int code)
{
	switch (code) {
		case DUPE:
			return "Dupe";
			break;
		case TRUMP:
			return "Trump";
			break;
		case BAD_FILE_NAMES:
			return "Bad File Names";
			break;
		case BAD_FOLDER_NAMES:
			return "Bad Folder Names";
			break;
		case BAD_TAGS:
			return "Bad Tags";
			break;
		case BAD_FORMAT:
			return "Disallowed Format";
			break;
		case DISCS_MISSING:
			return "Discs Missing";
			break;
		case DISCOGRAPHY:
			return "Discography";
			break;
		case EDITED_LOG:
			return "Edited Log";
			break;
		case INACCURATE_BITRATE:
			return "Inaccurate Bitrate";
			break;
		case LOW_BITRATE:
			return "Low Bitrate";
			break;
		case MUTT_RIP:
			return "Mutt Rip";
			break;
		case BAD_SOURCE:
			return "Disallowed Source";
			break;
		case ENCODE_ERRORS:
			return "Encode Errors";
			break;
		case BANNED:
			return "Specifically Banned";
			break;
		case TRACKS_MISSING:
			return "Tracks Missing";
			break;
		case TRANSCODE:
			return "Transcode";
			break;
		case CASSETTE:
			return "Unapproved Cassette";
			break;
		case UNSPLIT_ALBUM:
			return "Unsplit Album";
			break;
		case USER_COMPILATION:
			return "User Compilation";
			break;
		case WRONG_FORMAT:
			return "Wrong Format";
			break;
		case WRONG_MEDIA:
			return "Wrong Media";
			break;
		case AUDIENCE:
			return "Audience Recording";
			break;
		default:
			return "";
			break;
	}
}

/* Peers should be invisible if they are a leecher without
   download privs or their IP is invalid */
bool worker::peer_is_visible(user_ptr &u, peer *p) {
	return (p->left == 0 || u->can_leech()) && !p->invalid_ip;
}
