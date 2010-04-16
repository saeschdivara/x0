#include <x0/io/chunked_decoder.hpp>

namespace x0 {

chunked_decoder::chunked_decoder() :
	buffer_(),
	state_(START),
	size_(0)
{
}

buffer chunked_decoder::process(const buffer_ref& chunk, bool /*eof*/)
{
	auto i = chunk.begin();
	auto e = chunk.end();
	std::size_t offset = chunk.offset();

	while (i != e)
	{
		switch (state_)
		{
			case START:
				if (!std::isxdigit(*i))
					return buffer();

				state_ = SIZE_SPEC;
				// fall through
			case SIZE_SPEC:
				if (*i == '\r')
					state_ = LF1;
				else if (*i == '\n')
					state_ = CONTENT_START;
				else if (!std::isxdigit(*i))
					return buffer(); // parse error
				else
				{
					if (size_)
						size_ *= 16;

					if (std::isdigit(*i))
						size_ += *i - '0';
					else if (*i >= 'a' && *i <= 'f')
						size_ += 10 + *i - 'a';
					else // 'A' .. 'F'
						size_ += 10 + *i - 'A';
				}
				break;
			case CR1:
				if (*i != '\r')
					DEBUG("chunked_decoder: invalid char at state CR1: '%c'", *i);
				else
					state_ = LF1;
				break;
			case LF1:
				if (*i != '\n')
					DEBUG("chunked_decoder: invalid char at state LF1: '%c'", *i);
				else
					state_ = CONTENT;
				break;
			case CONTENT_START:
				DEBUG("content_start: size=%ld", size_);
				if (!size_)
				{
					DEBUG("END of chunked stream reached.");
					state_ = CR3;
					break;
				}
				// fall through
			case CONTENT:
				if (size_)
				{
#if 1
					buffer_.push_back(*i);
					--size_;
#else
					// TODO: add remaining chunk-body into buffer_ and inc i and offset properly.
					std::size_t size = std::min(size_, chunk.size());
					buffer_.push_back(chunk.buffer().ref(offset, size - (offset - chunk.offset())));

					offset += size - 1;
					i += size - 1;
#endif
					break;
				}
				else
				{
					state_ = CR2;
					// fall through
				}
			case CR2:
				if (*i != '\r')
					DEBUG("chunked_decoder: invalid char at state CR2: '%c'", *i);
				else
					state_ = LF2;
				break;
			case LF2:
				if (*i != '\n')
					DEBUG("chunked_decoder: invalid char at state LF2: '%c'", *i);

				state_ = SIZE_SPEC;
				size_ = 0;
				break;
			case CR3:
				if (*i != '\r')
					DEBUG("chunked_decoder: invalid char at state CR2: '%c'", *i);
				else
					state_ = LF3;
				break;
			case LF3:
				if (*i != '\n')
					DEBUG("chunked_decoder: invalid char at state LF2: '%c'", *i);

				state_ = END;
				size_ = 0;
				break;
			case END:
				break;
		}
		++i;
		++offset;
	}

	return std::move(buffer_);
}

} // namespace x0