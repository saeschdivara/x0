/* <plugins/director/DirectorPlugin.h>
 *
 * This file is part of the x0 web server project and is released under GPL-3.
 * http://www.xzero.io/
 *
 * (c) 2009-2013 Christian Parpart <trapni@gmail.com>
 */

#pragma once

#include <x0d/XzeroPlugin.h>
#include <x0/http/HttpServer.h>

#include <string>
#include <unordered_map>

namespace x0 {
	class HttpServer;
	class HttpRequest;
}

class Director;
class RoadWarrior;
class Backend;
class HaproxyApi;
struct RequestNotes;

class DirectorPlugin :
	public x0d::XzeroPlugin
{
private:
	std::unordered_map<std::string, Director*> directors_;
	RoadWarrior* roadWarrior_;
	HaproxyApi* haproxyApi_;
	x0::HttpServer::RequestHook::Connection postProcess_;

public:
	DirectorPlugin(x0d::XzeroDaemon* d, const std::string& name);
	~DirectorPlugin();

private:
	RequestNotes* requestNotes(x0::HttpRequest* r);

	void director_load(const x0::FlowParams& args, x0::FlowValue& result);
	void director_create(const x0::FlowParams& args, x0::FlowValue& result);

	void director_cache_key(x0::HttpRequest* r, const x0::FlowParams& args, x0::FlowValue& result);
	void director_cache_bypass(x0::HttpRequest* r, const x0::FlowParams& args, x0::FlowValue& result);

	bool director_balance(x0::HttpRequest* r, const x0::FlowParams& args);
	bool director_pass(x0::HttpRequest* r, const x0::FlowParams& args);
	bool director_api(x0::HttpRequest* r, const x0::FlowParams& args);
	bool director_fcgi(x0::HttpRequest* r, const x0::FlowParams& args);
	bool director_http(x0::HttpRequest* r, const x0::FlowParams& args);

	bool director_haproxy_monitor(x0::HttpRequest* r, const x0::FlowParams& args);
	bool director_haproxy_stats(x0::HttpRequest* r, const x0::FlowParams& args);

	bool internalServerError(x0::HttpRequest* r);
	Director* createDirector(const char* id);
	Backend* registerBackend(Director* director, const char* name, const char* url);
};
