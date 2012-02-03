#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <boost/asio.hpp>

#include "config.h"
#include "site_comm.h"

using boost::asio::ip::tcp;

site_comm::site_comm(config &config)
{
	conf = config;
}

bool site_comm::expire_token(int torrent, int user)
{
	try {
		boost::asio::io_service io_service;
	
		tcp::resolver resolver(io_service);
	 	tcp::resolver::query query(conf.site_host, "http");
		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
		tcp::resolver::iterator end;

		tcp::socket socket(io_service);
		boost::system::error_code error = boost::asio::error::host_not_found;
		while (error && endpoint_iterator != end) {
			socket.close();
			socket.connect(*endpoint_iterator++, error);
		}
		if (error) {
			throw boost::system::system_error(error);
		}

		boost::asio::streambuf request;
		std::ostream request_stream(&request);
		request_stream << "GET /tools.php?key=" << conf.site_password << "&type=expiretoken&action=ocelot&torrentid=" << torrent << "&userid=" << user << " HTTP/1.0\r\n";
		request_stream << "Host: " << conf.site_host << "\r\n";
		request_stream << "Accept: */*\r\n";
		request_stream << "Connection: close\r\n\r\n";

		boost::asio::write(socket, request);

		boost::asio::streambuf response;
		boost::asio::read_until(socket, response, "\r\n");

		std::istream response_stream(&response);
		std::string http_version;
		response_stream >> http_version;
		unsigned int status_code;
		response_stream >> status_code;
		std::string status_message;
		std::getline(response_stream, status_message);

		if (!response_stream || http_version.substr(0, 5) != "HTTP/") {
			std::cout << "Invalid response" << std::endl;
			return false;
		}

		if (status_code == 200) {
			return true;
		} else {
			std::cout << "Response returned with status code " << status_code << " when trying to expire a token!" << std::endl;;
			return false;
		}
	} catch (std::exception &e) {
		std::cout << "Exception: " << e.what() << std::endl;
		return false;
	}
	return true;
}

site_comm::~site_comm()
{
}
