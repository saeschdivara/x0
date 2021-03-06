#pragma once
/* <flow-tool/Flower.h>
 *
 * This file is part of the x0 web server project and is released under LGPL-3.
 * http://redmine.trapni.de/projects/x0
 *
 * (c) 2010-2013 Christian Parpart <trapni@gmail.com>
 */

#include <x0/flow/Flow.h>
#include <x0/flow/FlowParser.h>
#include <x0/flow/FlowRunner.h>
#include <x0/flow/FlowBackend.h>
#include <string>
#include <memory>
#include <cstdio>
#include <unistd.h>

using namespace x0;

class Flower : public x0::FlowBackend
{
private:
	std::string filename_;
	x0::FlowRunner runner_;
	size_t totalCases_;		// total number of cases ran
	size_t totalSuccess_;	// total number of succeed tests
	size_t totalFailed_;	// total number of failed tests

public:
	Flower();
	~Flower();

	int optimizationLevel() { return runner_.optimizationLevel(); }
	void setOptimizationLevel(int val) { runner_.setOptimizationLevel(val); }

	int run(const char *filename, const char *handler);
	int runAll(const char *filename);
	void dump();
	void clear();

private:
	bool onParseComplete(x0::Unit* unit);

	static void get_cwd(void *, x0::FlowParams& args, void *);
	static void flow_mkbuf(void *, x0::FlowParams& args, void *);
	static void flow_getbuf(void *, x0::FlowParams& args, void *);
	static void flow_getenv(void *, x0::FlowParams& args, void *);
	static void flow_error(void *, x0::FlowParams& args, void *);
	static void flow_finish(void *, x0::FlowParams& args, void *);
	static void flow_assert(void *, x0::FlowParams& args, void *);
	static void flow_fail(void *, x0::FlowParams& args, void *);
	static void flow_pass(void *, x0::FlowParams& args, void *);
	static void flow_assertFail(void *, x0::FlowParams& args, void *);

	static bool printValue(const x0::FlowValue& value, bool lf);
};
