#pragma once

#include <x0/http/HttpHeader.h>
#include <x0/Tokenizer.h>
#include <x0/Api.h>
#include <memory>
#include <vector>
#include <string>

namespace x0 {

class HttpRequest;

enum class VaryMatch {
	None,
	ValuesDiffer,
	Equals,
};

class X0_API Vary
{
public:
	explicit Vary(size_t count);
	~Vary();

	size_t size() const;

	const std::vector<std::string>& names() const;
	const std::vector<std::string>& values() const;

	VaryMatch match(const x0::HttpRequest* r) const;
	VaryMatch match(const Vary& other) const;

	/**
	 * Creates a Vary object, based on the Response request header
	 *
	 * @param vary Vary HTTP response header, a comma seperated list of request headers.
	 * @param requestHeaders list of request headers
	 *
	 * @return a Vary instance reflecting the list of varying fields.
	 */
	template<typename T>
	static std::unique_ptr<Vary> create(const std::string& vary, const std::vector<HttpHeader<T>>& requestHeaders);

	/**
	 * Creates a Vary object, based on the Response request header
	 */
	static std::unique_ptr<Vary> create(const x0::HttpRequest* r);

	class iterator {
	public:
		iterator() {}
		iterator(Vary* vary, size_t i) : 
			vary_(vary), i_(i), e_(vary->size()) {}

		iterator& operator++() {
			if (i_ != e_)
				++i_;

			return *this;
		}

		bool operator==(const iterator& other) { return i_ == other.i_; }
		bool operator!=(const iterator& other) { return !(*this == other); }

		std::pair<const std::string&, const std::string&>  operator*() const {
			return std::make_pair(vary_->names()[i_], vary_->values()[i_]);
		}

		const std::string& name() const { return vary_->names()[i_]; }
		const std::string& value() const { return vary_->values()[i_]; }

	private:
		Vary* vary_;
		size_t i_;
		size_t e_;
	};

	iterator begin() { return iterator(this, 0); }
	iterator end() { return iterator(this, size()); }

private:
	template<typename T, typename U>
	static inline T find(const U& name, const std::vector<HttpHeader<T>>& requestHeaders);

private:
	std::vector<std::string> names_;
	std::vector<std::string> values_;
};

// {{{ inlines
inline size_t Vary::size() const
{
	return names_.size(); // this is okay since names.size() always equals values.size()
}

inline const std::vector<std::string>& Vary::names() const
{
	return names_;
}

inline const std::vector<std::string>& Vary::values() const
{
	return values_;
}

template<typename T, typename U>
inline T Vary::find(const U& name, const std::vector<HttpHeader<T>>& requestHeaders)
{
	for (auto& i: requestHeaders)
		if (iequals(name, i.name))
			return i.value;

	return T();
}

template<typename T>
std::unique_ptr<Vary> Vary::create(const std::string& varyHeader, const std::vector<HttpHeader<T>>& requestHeaders)
{
//	if (varyHeader.empty())
//		return std::unique_ptr<Vary>();

	Buffer header;
	header.push_back(varyHeader);

	std::unique_ptr<Vary> vary;
	auto tokens = Tokenizer<BufferRef>::tokenize(header.ref(), ", ");
	vary.reset(new Vary(tokens.size()));
	for (size_t i = 0, e = tokens.size(); i != e; ++i) {
		auto name = tokens[i];
		vary->names_[i] = std::string(name.data(), name.size());
		vary->values_[i] = find(name, requestHeaders);
	}

	return vary;
}
// }}}

} // namespace x0
