#include "user.h"

user::user(int uid, bool leech, bool protect) : id(uid), leechstatus(leech), protect_ip(protect) {
	stats.leeching = 0;
	stats.seeding = 0;
}

int user::get_id() {
	return id;
}

bool user::is_protected() {
	return protect_ip;
}

void user::set_protected(bool status) {
	protect_ip = status;
}

bool user::can_leech() {
	return leechstatus;
}

void user::set_leechstatus(bool status) {
	leechstatus = status;
}

// Stats methods
unsigned int user::get_leeching() {
	return stats.leeching;
}

unsigned int user::get_seeding() {
	return stats.seeding;
}

void user::decr_leeching() {
	stats.leeching--;
}

void user::decr_seeding() {
	stats.seeding--;
}

void user::incr_leeching() {
	stats.leeching++;
}

void user::incr_seeding() {
	stats.seeding++;
}
