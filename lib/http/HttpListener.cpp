/* <x0/HttpListener.cpp>
 *
 * This file is part of the x0 web server project and is released under LGPL-3.
 * http://www.xzero.ws/
 *
 * (c) 2009-2010 Christian Parpart <trapni@gentoo.org>
 */

#include <x0/http/HttpListener.h>
#include <x0/http/HttpConnection.h>
#include <x0/http/HttpServer.h>
#include <x0/SocketDriver.h>
#include <x0/sysconfig.h>
#include <fcntl.h>

namespace x0 {

#if !defined(NDEBUG)
#	define TRACE(msg...) (this->debug(msg))
#else
#	define TRACE(msg...) do {} while (0)
#endif

HttpListener::HttpListener(HttpServer& srv) : 
#ifndef NDEBUG
	Logging("HttpListener"),
#endif
	socket_(srv.loop()),
	server_(srv),
	errorCount_(0)
{
	socket_.set<HttpListener, &HttpListener::callback>(this);
}

HttpListener::~HttpListener()
{
	TRACE("~HttpListener(): %s:%d", socket_.address().c_str(), socket_.port());
	stop();
}

int HttpListener::backlog() const
{
	return socket_.backlog();
}

void HttpListener::setBacklog(int value)
{
	socket_.setBacklog(value);
}

bool HttpListener::open(const std::string& unixPath)
{
	return socket_.open(unixPath, O_CLOEXEC | O_NONBLOCK);
}

bool HttpListener::open(const std::string& address, int port)
{
	return socket_.open(address, port, O_CLOEXEC | O_NONBLOCK);
}

void HttpListener::stop()
{
	TRACE("stopping");
	socket_.close();
}

void HttpListener::callback(Socket* cs, ServerSocket*)
{
	server_.selectWorker()->enqueue(std::make_pair(cs, this));
}

} // namespace x0
