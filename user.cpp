#include "user.h"

user::user(userid_t uid, bool leech, bool protect) : id(uid), deleted(false), leechstatus(leech), protect_ip(protect) {
	stats.leeching = 0;
	stats.seeding = 0;
}
