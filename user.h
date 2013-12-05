class user {
	private:
		int id;
		bool leechstatus;
		bool protect_ip;
		struct {
			unsigned int leeching;
			unsigned int seeding;
		} stats;
	public:
		user(int uid, bool leech, bool protect);
		int get_id();
		bool is_protected();
		void set_protected(bool status);
		bool can_leech();
		void set_leechstatus(bool status);
		void decr_leeching();
		void decr_seeding();
		void incr_leeching();
		void incr_seeding();
		unsigned int get_leeching();
		unsigned int get_seeding();
};
