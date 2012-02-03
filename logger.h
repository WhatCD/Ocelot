#ifndef OCELOT_LOGGER_H
#define OCELOT_LOGGER_H

#include <string>
#include <iostream>
#include <fstream>

#include <boost/thread/mutex.hpp>

class logger {

        public:
                logger(std::string filename);
		virtual ~logger(void);
                bool log(std::string msg);
		static logger* get_instance(void);

        protected:

        private:
                static logger* singletonInstance_;
                boost::mutex log_lock_;
		std::ofstream log_file_;
};

#endif
