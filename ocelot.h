#include <string>
#include <map>
#include <vector>
#include <unordered_map>
#include <set>
#include <boost/thread/thread.hpp>

#ifndef OCELOT_H
#define OCELOT_H

typedef struct {
	int userid;
	unsigned int port;
	int64_t uploaded;
	int64_t downloaded;
	int64_t corrupt;
	int64_t left;
	time_t last_announced;
	time_t first_announced;
	unsigned int announces;
	bool visible;
	bool invalid_ip;
	std::string ip_port;
	std::string ip;
} peer;

typedef std::map<std::string, peer> peer_list;

enum freetype { NORMAL, FREE, NEUTRAL };

typedef struct {
	int id;
	time_t last_seeded;
	int64_t balance;
	int completed;
	freetype free_torrent;
	time_t last_flushed;
	peer_list seeders;
	peer_list leechers;
	std::string last_selected_seeder;
	std::set<int> tokened_users;
} torrent;

typedef struct {
	int id;
	bool can_leech;
	bool protect_ip;
} user;

enum {
	DUPE, // 0
	TRUMP, // 1
	BAD_FILE_NAMES, // 2 
	BAD_FOLDER_NAMES, // 3
	BAD_TAGS, // 4
	BAD_FORMAT, // 5
	DISCS_MISSING, // 6 
	DISCOGRAPHY,// 7
	EDITED_LOG,// 8
	INACCURATE_BITRATE, // 9
	LOW_BITRATE, // 10
	MUTT_RIP,// 11
	BAD_SOURCE,// 12
	ENCODE_ERRORS,// 13
	BANNED, // 14
	TRACKS_MISSING,// 15
	TRANSCODE, // 16
	CASSETTE, // 17
	UNSPLIT_ALBUM, // 18
	USER_COMPILATION, // 19
	WRONG_FORMAT, // 20
	WRONG_MEDIA, // 21
	AUDIENCE // 22
};

typedef struct {
	int reason;
	time_t time;
} del_message;


typedef std::unordered_map<std::string, torrent> torrent_list;
typedef std::unordered_map<std::string, user> user_list;

#endif
