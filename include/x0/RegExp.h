/* <flow/RegExp.h>
 *
 * This file is part of the x0 web server project and is released under LGPL-3.
 * http://redmine.xzero.io/projects/flow
 *
 * (c) 2009-2013 Christian Parpart <trapni@gmail.com>
 */

#ifndef sw_flow_RegExp_h
#define sw_flow_RegExp_h

#include <x0/Api.h>
#include <pcre.h>
#include <string>
#include <vector>

namespace x0 {

class BufferRef;

class X0_API RegExp
{
private:
	std::string pattern_;
	pcre *re_;

public:
	typedef std::vector<std::pair<const char*, size_t>> Result;

public:
	explicit RegExp(const std::string& pattern);
	RegExp();
	RegExp(const RegExp& v);
	~RegExp();

	bool match(const char *buffer, size_t size, Result* result = nullptr) const;
	bool match(const BufferRef& buffer, Result* result = nullptr) const;
	bool match(const char *cstring, Result* result = nullptr) const;

	const std::string& pattern() const { return pattern_; }
	const char *c_str() const;
};

class X0_API RegExpContext
{
public:
	RegExpContext();
	virtual ~RegExpContext();

	RegExp::Result* regexMatch() {
		if (!regexMatch_)
			regexMatch_ = new RegExp::Result();

		return regexMatch_;
	}

private:
	RegExp::Result* regexMatch_;
};

} // namespace x0

#endif
