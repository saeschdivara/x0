/* <x0/HttpConnection.cpp>
 *
 * This file is part of the x0 web server project and is released under LGPL-3.
 * http://www.xzero.ws/
 *
 * (c) 2009-2010 Christian Parpart <trapni@gentoo.org>
 */

#include <x0/http/HttpConnection.h>
#include <x0/http/HttpListener.h>
#include <x0/http/HttpRequest.h>
#include <x0/SocketDriver.h>
#include <x0/StackTrace.h>
#include <x0/Socket.h>
#include <x0/Types.h>
#include <x0/sysconfig.h>

#include <functional>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#if !defined(NDEBUG)
#	define TRACE(msg...) this->debug(msg)
#else
#	define TRACE(msg...) do { } while (0)
#endif

#define X0_HTTP_STRICT 1

namespace x0 {

/**
 * \class HttpConnection
 * \brief represents an HTTP connection handling incoming requests.
 *
 * The \p HttpConnection object is to be allocated once an HTTP client connects
 * to the HTTP server and was accepted by the \p HttpListener.
 * It will also own the respective request and response objects created to serve
 * the requests passed through this connection.
 */

/** initializes a new connection object, created by given listener.
 * \param lst the listener object that created this connection.
 * \note This triggers the onConnectionOpen event.
 */
HttpConnection::HttpConnection(HttpListener& lst, HttpWorker& w, int fd) :
	HttpMessageProcessor(HttpMessageProcessor::REQUEST),
	secure(false),
	listener_(lst),
	worker_(w),
	socket_(0),
	active_(true), // when this is constricuted, it *must* be active right now :) 
	buffer_(8192),
	offset_(0),
	request_count_(0),
	request_(0),
	abortHandler_(nullptr),
	abortData_(nullptr),
	source_(),
	sink_(nullptr),
	onWriteComplete_(),
	bytesTransferred_(0)
#if !defined(NDEBUG)
	, ctime_(ev_now(loop()))
#endif
{
	socket_ = listener_.socketDriver()->create(loop(), fd, lst.addressFamily());
	sink_.setSocket(socket_);

#if !defined(NDEBUG)
	debug(true);
	static std::atomic<unsigned long long> id(0);
	setLoggingPrefix("Connection[%d,%s:%d]", ++id, remoteIP().c_str(), remotePort());
#endif

	TRACE("fd=%d", socket_->handle());

#if defined(TCP_NODELAY)
	if (worker_.server_.tcp_nodelay())
		socket_->setTcpNoDelay(true);
#endif

	worker_.server_.onConnectionOpen(this);
}

/** releases all connection resources  and triggers the onConnectionClose event.
 */
HttpConnection::~HttpConnection()
{
	delete request_;
	request_ = nullptr;

	clearCustomData();

	TRACE("destructing");
	//TRACE("Stack Trace:\n%s", StackTrace().c_str());

	worker_.release(this);

	try
	{
		worker_.server_.onConnectionClose(this); // we cannot pass a shared pointer here as use_count is already zero and it would just lead into an exception though
	}
	catch (...)
	{
		TRACE("unexpected exception");
	}

	if (socket_)
	{
		delete socket_;
#if !defined(NDEBUG)
		socket_ = 0;
#endif
	}
}

void HttpConnection::io(Socket *, int revents)
{
	TRACE("io(mode=%s)", socket_->mode_str());
	active_ = true;

	if (revents & Socket::Read)
		processInput();

	if (revents & Socket::Write)
		processOutput();

	if (isClosed())
		delete this;
	else
		active_ = false;
}

void HttpConnection::timeout(Socket *)
{
	TRACE("timed out");

//	ev_unloop(loop(), EVUNLOOP_ONE);

	delete this;
}

#if defined(WITH_SSL)
bool HttpConnection::isSecure() const
{
	return listener_.isSecure();
}
#endif

/** start first async operation for this HttpConnection.
 *
 * This is done by simply registering the underlying socket to the the I/O service
 * to watch for available input.
 * \see stop()
 * :
 */
void HttpConnection::start()
{
	if (socket_->state() == Socket::Handshake)
	{
		TRACE("start: handshake.");
		socket_->handshake<HttpConnection, &HttpConnection::handshakeComplete>(this);
	}
	else
	{
#if defined(TCP_DEFER_ACCEPT)
		TRACE("start: processing input");
		// it is ensured, that we have data pending, so directly start reading
		processInput();
		TRACE("start: processing input done");

		// destroy connection in case the above caused connection-close
		// XXX this is usually done within HttpConnection::io(), but we are not.
		if (isClosed())
			delete this;
		else
			active_ = false;
#else
		TRACE("start: startRead.");
		// client connected, but we do not yet know if we have data pending
		startRead();
#endif
	}
}

void HttpConnection::handshakeComplete(Socket *)
{
	TRACE("handshakeComplete() socketState=%s", socket_->state_str());

	if (socket_->state() == Socket::Operational)
		startRead();
	else
	{
		TRACE("handshakeComplete(): handshake failed\n%s", StackTrace().c_str());
		close();
	}
}

inline bool url_decode(BufferRef& url)
{
	std::size_t left = url.offset();
	std::size_t right = left + url.size();
	std::size_t i = left; // read pos
	std::size_t d = left; // write pos
	Buffer& value = url.buffer();

	while (i != right)
	{
		if (value[i] == '%')
		{
			if (i + 3 <= right)
			{
				int ival;
				if (hex2int(value.begin() + i + 1, value.begin() + i + 3, ival))
				{
					value[d++] = static_cast<char>(ival);
					i += 3;
				}
				else
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		else if (value[i] == '+')
		{
			value[d++] = ' ';
			++i;
		}
		else if (d != i)
		{
			value[d++] = value[i++];
		}
		else
		{
			++d;
			++i;
		}
	}

	url = value.ref(left, d - left);
	return true;
}

void HttpConnection::messageBegin(BufferRef&& method, BufferRef&& uri, int version_major, int version_minor)
{
	TRACE("messageBegin('%s', '%s', HTTP/%d.%d)", method.str().c_str(), uri.str().c_str(), version_major, version_minor);

	request_ = new HttpRequest(*this);

	request_->method = std::move(method);

	request_->uri = std::move(uri);
	url_decode(request_->uri);

	std::size_t n = request_->uri.find("?");
	if (n != std::string::npos)
	{
		request_->path = request_->uri.ref(0, n);
		request_->query = request_->uri.ref(n + 1);
	}
	else
	{
		request_->path = request_->uri;
	}

	request_->httpVersionMajor = version_major;
	request_->httpVersionMinor = version_minor;
}

void HttpConnection::messageHeader(BufferRef&& name, BufferRef&& value)
{
	if (iequals(name, "Host"))
	{
		auto i = value.find(':');
		if (i != BufferRef::npos)
			request_->hostname = value.ref(0, i);
		else
			request_->hostname = value;
	}

	request_->requestHeaders.push_back(HttpRequestHeader(std::move(name), std::move(value)));
}

bool HttpConnection::messageHeaderEnd()
{
	TRACE("messageHeaderEnd()");

#if X0_HTTP_STRICT
	BufferRef expectHeader = request_->requestHeader("Expect");
	bool content_required = request_->method == "POST" || request_->method == "PUT";

	if (content_required && request_->connection.contentLength() == -1)
	{
		request_->status = HttpError::LengthRequired;
		request_->finish();
	}
	else if (!content_required && request_->contentAvailable())
	{
		request_->status = HttpError::BadRequest; // FIXME do we have a better status code?
		request_->finish();
	}
	else if (expectHeader)
	{
		request_->expectingContinue = equals(expectHeader, "100-continue");

		if (!request_->expectingContinue || !request_->supportsProtocol(1, 1))
		{
			request_->status = HttpError::ExpectationFailed;
			request_->finish();
		}
		else
			worker_.handleRequest(request_);
	}
	else
		worker_.handleRequest(request_);
#else
	worker_.handleRequest(request_);
#endif

	return true;
}

bool HttpConnection::messageContent(BufferRef&& chunk)
{
	TRACE("messageContent(#%ld)", chunk.size());

	if (request_)
		request_->onRequestContent(std::move(chunk));

	return true;
}

bool HttpConnection::messageEnd()
{
	TRACE("messageEnd()");

	// increment the number of fully processed requests
	++request_count_;

	// XXX is this really required? (meant to mark the request-content EOS)
	if (request_)
		request_->onRequestContent(BufferRef());

	// allow continueing processing possible further requests
	return true;
}

/** Resumes processing the <b>next</b> HTTP request message within this connection.
 *
 * This method may only be invoked when the current/old request has been fully processed,
 * and is though only be invoked from within the finish handler of \p HttpRequest.
 *
 * \see HttpRequest::finish()
 */
void HttpConnection::resume()
{
	if (socket()->tcpCork())
		socket()->setTcpCork(false);

	assert(request_ != nullptr);
	delete request_;
	request_ = nullptr;

	// wait for new request message, if nothing in buffer
	if (offset_ == buffer_.size())
		startRead();
}

void HttpConnection::startRead()
{
	int timeout = request_count_ && state() == MESSAGE_BEGIN
		? worker_.server_.max_keep_alive_idle()
		: worker_.server_.max_read_idle();

	if (timeout > 0)
		socket_->setTimeout<HttpConnection, &HttpConnection::timeout>(this, timeout);

	socket_->setReadyCallback<HttpConnection, &HttpConnection::io>(this);
	socket_->setMode(Socket::Read);
}

/**
 * This method gets invoked when there is data in our HttpConnection ready to read.
 *
 * We assume, that we are in request-parsing state.
 */
void HttpConnection::processInput()
{
	TRACE("processInput()");

	ssize_t rv = socket_->read(buffer_);

	if (rv < 0) // error
	{
		if (errno == EAGAIN || errno == EINTR)
		{
			startRead();
			ev_unloop(loop(), EVUNLOOP_ONE);
		}
		else
		{
			TRACE("processInput(): %s", strerror(errno));
			close();
		}
	}
	else if (rv == 0) // EOF
	{
		TRACE("processInput(): (EOF)");

		if (abortHandler_) {
			socket_->setMode(Socket::None);
			abortHandler_(abortData_);
		} else
			close();
	}
	else
	{
		TRACE("processInput(): read %ld bytes", rv);
		TRACE("%s", buffer_.ref(buffer_.size() - rv).str().c_str());

		process();

		TRACE("processInput(): done process()ing; mode=%s, fd=%d, request=%p",
			socket_->mode_str(), socket_->handle(), request_);
	}
}

/**
 * Writes as much as it wouldn't block of the response stream into the underlying socket.
 *
 */
void HttpConnection::processOutput()
{
	TRACE("processOutput()");

	for (;;)
	{
		ssize_t rv = source_.sendto(sink_);

		TRACE("processOutput(): sendto().rv=%ld %s", rv, rv < 0 ? strerror(errno) : "");
		// TODO make use of source_->eof()

		if (rv > 0) // source (partially?) written
		{
			bytesTransferred_ += rv;
		}
		else if (rv == 0) // source fully written
		{
			TRACE("processOutput(): source fully written");
			source_.reset();

			if (onWriteComplete_)
				onWriteComplete_(0, bytesTransferred_);

			//onWriteComplete_ = CompletionHandlerType();
			break;
		}
		else if (errno == EAGAIN || errno == EINTR) // completing write would block
		{
			socket_->setReadyCallback<HttpConnection, &HttpConnection::io>(this); // XXX should be same
			socket_->setMode(Socket::Write);
			break;
		}
		else // an error occurred
		{
			TRACE("processOutput(): write error (%d): %s", errno, strerror(errno));
			source_.reset();

			if (onWriteComplete_)
				onWriteComplete_(errno, bytesTransferred_);

			//onWriteComplete_ = CompletionHandlerType();
			close();
			break;
		}
	}
}

/** closes this HttpConnection, possibly deleting this object (or propagating delayed delete).
 */
void HttpConnection::close()
{
	TRACE("(%p).close() (active=%d)", this, active_);
	//TRACE("Stack Trace:%s\n", StackTrace().c_str());

	socket_->close();

	if (request_) {
		delete request_;
		request_ = nullptr;
	}

	if (!active_) {
		delete this;
	}
}

/** processes a (partial) request from buffer's given \p offset of \p count bytes.
 */
void HttpConnection::process()
{
	TRACE("process: offset=%ld, size=%ld (before processing)", offset_, buffer_.size());

	std::error_code ec = HttpMessageProcessor::process(
			buffer_.ref(offset_, buffer_.size() - offset_),
			offset_);

	TRACE("process: offset=%ld, bs=%ld, ec=%s, state=%s (after processing)",
			offset_, buffer_.size(), ec.message().c_str(), state_str());

#if 0
	if (state() == HttpMessageProcessor::MESSAGE_BEGIN) {
		// TODO reenable buffer reset (or reuse another for content! to be more huge-body friendly)
		offset_ = 0;
		buffer_.clear();
	}
#endif

	if (isClosed())
		return;

	if (ec == HttpMessageError::Partial)
		startRead();
	else if (!request_)
		return;
	else if (ec && ec != HttpMessageError::Aborted)
	{
		// -> send stock response: BAD_REQUEST
		request_->status = HttpError::BadRequest;
		request_->finish();
	}
}

std::string HttpConnection::remoteIP() const
{
	return socket_->remoteIP();
}

unsigned int HttpConnection::remotePort() const
{
	return socket_->remotePort();
}

std::string HttpConnection::localIP() const
{
	return listener_.address();
}

unsigned int HttpConnection::localPort() const
{
	return socket_->localPort();
}

} // namespace x0
