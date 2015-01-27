#ifndef OCELOT_CONFIG_H
#define OCELOT_CONFIG_H

#include <string>
#include <map>

class confval {
	private:
		bool bool_val;
		uint32_t uint_val;
		std::string str_val;
		enum {
			CONF_NONEXISTENT,
			CONF_BOOL,
			CONF_UINT,
			CONF_STR,
		} val_type;
	public:
		confval();
		confval(bool value);
		confval(uint32_t value);
		confval(const char * value);
		uint32_t get_uint();
		bool get_bool();
		std::string get_str();
		void set(const std::string &val);
};

class config {
	private:
		template <typename T> void add(const std::string &setting_name, T value);
		std::string trim(const std::string str);
		void init();
		confval * get(const std::string &setting_name);
		std::map<std::string, confval> settings;
		confval * dummy_setting;
	public:
		config();
		void load(std::istream &conf_file);
		void load(const std::string &conf_file_path, std::istream &conf_file);
		void reload();
		bool get_bool(const std::string &setting_name);
		uint32_t get_uint(const std::string &setting_name);
		std::string get_str(const std::string &setting_name);
		void set(const std::string &setting_name, const std::string &value);
};

template <typename T> void config::add(const std::string &setting_name, T value) {
	confval setting(value);
	settings[setting_name] = setting;
}
#endif
