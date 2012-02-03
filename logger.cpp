#include "logger.h"

logger* logger::singletonInstance_ = 0;

logger::logger(std::string filename) {
	logger::log_file_.open(filename.c_str(), std::ios::out);
	if(logger::log_file_.is_open()) {
		singletonInstance_ = this;
	}
}

logger::~logger(void) {
	if(log_file_.is_open()) {
		log_file_.close();
	}
	singletonInstance_ = 0;
}

logger *logger::get_instance(void) {
	if(singletonInstance_ != 0) {
		return singletonInstance_;
	}
	return NULL;
}

bool logger::log(std::string msg) {
	boost::mutex::scoped_lock lock(log_lock_);
	if(log_file_.is_open()) {
		log_file_ << msg << std::endl;
		log_file_.flush();
		return true;
	}
	return false;
}
