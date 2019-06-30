#include <time.h>
#include <sys/time.h>
#include <socket.h>
#include <sys/socket.h>
#include <netdb.h>
#include <mbed_config.h>
#include <mbed.h>
#include <NetworkStack.h>
#include <ticker_api.h>
#include <array>
#include <sys/syslimits.h>
#include <nsapi_types.h>
#include <netinet/tcp.h>

#define SOCK_CLOEXEC   02000000
#define SOCK_NONBLOCK  04000

const char *sec2str(nsapi_security_t sec)
{
	switch (sec) {
	case NSAPI_SECURITY_NONE:
		return "None";
	case NSAPI_SECURITY_WEP:
		return "WEP";
	case NSAPI_SECURITY_WPA:
		return "WPA";
	case NSAPI_SECURITY_WPA2:
		return "WPA2";
	case NSAPI_SECURITY_WPA_WPA2:
		return "WPA/WPA2";
	case NSAPI_SECURITY_UNKNOWN:
	default:
		return "Unknown";
	}
}

extern "C" void _kill()
{
}

extern "C" int _getpid()
{
	return 1;
}

extern "C" int gettimeofday(struct timeval *__restrict __p, void *__restrict __tz)
{
	us_timestamp_t time;
	if (!__p) return 0;
	time = ticker_read_us(get_us_ticker_data());
	__p->tv_sec = time / 1000000;
	__p->tv_usec = time - (__p->tv_sec * 1000000);
	return 0;
}

extern "C" int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
	us_timestamp_t now;

	if (clock_id != CLOCK_REALTIME) {
		errno = EINVAL;
		return -1;
	}

	now = ticker_read_us(get_us_ticker_data());
	tp->tv_sec = now / 1000000;
	tp->tv_nsec = (now % 1000000) * 1000;

	return 0;
}

extern "C" char *basename(char *s)
{
	size_t i, j;
	if (!s || !*s) return ".";
	i = strlen(s) - 1;
	for (j = 0; j <= i; j++) if (s[j] == ':') { s = &s[j + 1]; i -= j; break; }
	for (; i&&s[i] == '/'; i--) s[i] = 0;
	for (; i&&s[i - 1] != '/'; i--);
	return s + i;
}

extern "C" uint16_t htons(uint16_t h)
{
	return (h << 8) | (h >> 8);
}

extern "C" uint16_t ntohs(uint16_t n)
{
	return (n << 8) | (n >> 8);
}

extern "C" int socketpair(int domain, int type, int protocol, int sv[2])
{
	errno = ENOTSUP;
	return -1;
}

extern "C" int dup2(int oldfd, int newfd)
{
	errno = ENOTSUP;
	return -1;
}

extern "C" int execl(const char *path, const char *arg, ...)
{
	errno = ENOTSUP;
	return -1;
}

extern "C" pid_t waitpid(pid_t pid, int *status, int options)
{
	errno = ENOTSUP;
	return -1;
}

extern "C" int _fork()
{
	errno = ENOTSUP;
	return -1;
}

/*extern "C" int custom_rand_generate_seed(uint8_t* output, int32_t sz)
{
	us_timestamp_t now;
	int32_t i;

	now = ticker_read_us(get_us_ticker_data());
	srand(now);

	for (i = 0; i < sz; i++)
		output[i] = rand();

	return 0;
}*/

class FilePointer {
public:
	FilePointer(int fd, nsapi_version_t version, nsapi_protocol_t protocol, unsigned int flags)
		: fd(fd), version(version), protocol(protocol), flags(flags), _stack(0), _socket(0), backlog(0),
		_timeout(0), _pending(0), in_accept(0),
		_read_in_progress(0), _write_in_progress(0),
		readevt_r(0), readevt_w(0),
		writeevt_r(0), writeevt_w(0),
		errorevt_r(0), errorevt_w(0)
	{
	}
public:
	int fd;
	nsapi_version_t version;
	nsapi_protocol_t protocol;
	unsigned int flags;
	int backlog;
	union {
		sockaddr_in raddr4;
		sockaddr_in6 raddr6;
	};
	mbed::Callback<void()> _event;
	rtos::Mutex _lock;
	rtos::Semaphore _accept_sem;
	bool _read_in_progress;
	bool _write_in_progress;
	int readevt_r;
	int readevt_w;
	int writeevt_r;
	int writeevt_w;
	int errorevt_r;
	int errorevt_w;

	nsapi_error_t open(NetworkStack *stack);
	nsapi_error_t close();
	nsapi_error_t bind(const SocketAddress &address);
	nsapi_error_t listen(int backlog = 1);
	nsapi_error_t accept(FilePointer *connection, SocketAddress *address = NULL);
	nsapi_error_t connect(const SocketAddress &address);
	nsapi_size_or_error_t send(const void *data, nsapi_size_t size);
	nsapi_size_or_error_t recv(void *data, nsapi_size_t size);
	nsapi_size_or_error_t sendto(const SocketAddress &address, const void *data, nsapi_size_t size);
	nsapi_size_or_error_t recvfrom(SocketAddress *address, void *buffer, nsapi_size_t size);
	nsapi_error_t setsockopt(int level, int optname, const void *optval, unsigned optlen);
	nsapi_error_t getsockopt(int level, int optname, void *optval, unsigned *optlen);
private:
	NetworkStack *_stack;
	nsapi_socket_t _socket;
	uint32_t _timeout;
	volatile unsigned _pending;
	bool in_accept;
protected:
	void event();
public:
	NetworkStack *get_stack() { return _stack; }
	void get_ip_address(SocketAddress *address)
	{
		new (address) SocketAddress(_stack->get_ip_address());
	}
};

static std::array<FilePointer *, 8 * sizeof(uint32_t) - 1> fp_table;
static SingletonPtr<PlatformMutex> socket_mutex;
static rtos::EventFlags _event_flag;

int new_fd()
{
	for (int fd = 0; fd < fp_table.size(); fd++) {
		if (fp_table[fd] != NULL)
			continue;

		return fd;
	}

	return -1;
}

int delete_fp(FilePointer *fp)
{
	for (int fd = 0; fd < fp_table.size(); fd++) {
		if (fp_table[fd] != fp)
			continue;

		fp_table[fd] = NULL;
		break;
	}

	delete fp;

	return 0;
}

FilePointer *fd_to_fp(int fd)
{
	for (auto fp : fp_table) {
		if ((fp != NULL) && (fp->fd == fd))
			return fp;
	}

	return NULL;
}

nsapi_error_t FilePointer::open(NetworkStack *stack)
{
	_lock.lock();

	nsapi_socket_t socket;
	nsapi_error_t err = stack->socket_open(&socket, protocol);
	if (err) {
		_lock.unlock();
		return err;
	}

	_stack = stack;
	_socket = socket;
	_event = mbed::callback(this, &FilePointer::event);
	_stack->socket_attach(_socket, mbed::Callback<void()>::thunk, &_event);

	_lock.unlock();
	return NSAPI_ERROR_OK;
}

nsapi_error_t FilePointer::close()
{
	_lock.lock();

	nsapi_error_t ret = NSAPI_ERROR_OK;
	if (_socket) {
		_stack->socket_attach(_socket, 0, 0);
		nsapi_socket_t socket = _socket;
		_socket = 0;
		ret = _stack->socket_close(socket);
	}
	_stack = 0;

	// Wakeup anything in a blocking operation
	// on this socket
	event();

	_lock.unlock();
	return ret;
}

nsapi_error_t FilePointer::bind(const SocketAddress &address)
{
	_lock.lock();
	nsapi_error_t ret;

	if (!_socket) {
		ret = NSAPI_ERROR_NO_SOCKET;
	}
	else {
		ret = _stack->socket_bind(_socket, address);
	}

	_lock.unlock();
	return ret;
}

nsapi_error_t FilePointer::listen(int backlog)
{
	_lock.lock();
	nsapi_error_t ret;

	if (!_socket) {
		ret = NSAPI_ERROR_NO_SOCKET;
	}
	else {
		ret = _stack->socket_listen(_socket, backlog);
	}

	_lock.unlock();
	return ret;
}

nsapi_error_t FilePointer::accept(FilePointer *connection, SocketAddress *address)
{
	_lock.lock();
	nsapi_error_t ret;

	in_accept = true;
	while (true) {
		if (!_socket) {
			ret = NSAPI_ERROR_NO_SOCKET;
			break;
		}

		_pending = 0;
		void *socket;
		ret = _stack->socket_accept(_socket, &socket, address);

		if (0 == ret) {
			connection->_lock.lock();

			if (connection->_socket) {
				connection->close();
			}

			connection->_stack = _stack;
			connection->_socket = socket;
			connection->_event = mbed::Callback<void()>(connection, &FilePointer::event);
			_stack->socket_attach(socket, &mbed::Callback<void()>::thunk, &connection->_event);

			connection->_lock.unlock();
			break;
		}
		else if (NSAPI_ERROR_WOULD_BLOCK != ret) {
			break;
		}
		else {
			int32_t count;

			// Release lock before blocking so other threads
			// accessing this object aren't blocked
			_lock.unlock();
			count = _accept_sem.wait(_timeout);
			_lock.lock();

			if (count < 1) {
				// Semaphore wait timed out so break out and return
				ret = NSAPI_ERROR_WOULD_BLOCK;
				break;
			}
		}
	}
	in_accept = false;

	_lock.unlock();
	return ret;
}

nsapi_error_t FilePointer::connect(const SocketAddress &address)
{
	_lock.lock();
	nsapi_error_t ret;

	// If this assert is hit then there are two threads
	// performing a send at the same time which is undefined
	// behavior
	MBED_ASSERT(!_write_in_progress);
	_write_in_progress = true;

	bool blocking_connect_in_progress = false;

	while (true) {
		if (!_socket) {
			ret = NSAPI_ERROR_NO_SOCKET;
			break;
		}

		_pending = 0;
		ret = _stack->socket_connect(_socket, address);
		if ((_timeout == 0) || !(ret == NSAPI_ERROR_IN_PROGRESS || ret == NSAPI_ERROR_ALREADY)) {
			break;
		}
		else {
			blocking_connect_in_progress = true;

			uint32_t flag;

			// Release lock before blocking so other threads
			// accessing this object aren't blocked
			_lock.unlock();
			flag = _event_flag.wait_any(fd, _timeout);
			_lock.lock();

			if (flag & osFlagsError) {
				// Timeout break
				break;
			}
		}
	}

	_write_in_progress = false;

	/* Non-blocking connect gives "EISCONN" once done - convert to OK for blocking mode if we became connected during this call */
	if (ret == NSAPI_ERROR_IS_CONNECTED && blocking_connect_in_progress) {
		ret = NSAPI_ERROR_OK;
	}

	_lock.unlock();
	return ret;
}

nsapi_size_or_error_t FilePointer::send(const void *data, nsapi_size_t size)
{
	_lock.lock();
	const uint8_t *data_ptr = static_cast<const uint8_t *>(data);
	nsapi_size_or_error_t ret;
	nsapi_size_t written = 0;

	// If this assert is hit then there are two threads
	// performing a send at the same time which is undefined
	// behavior
	MBED_ASSERT(!_write_in_progress);
	_write_in_progress = true;

	// Unlike recv, we should write the whole thing if blocking. POSIX only
	// allows partial as a side-effect of signal handling; it normally tries to
	// write everything if blocking. Without signals we can always write all.
	while (true) {
		if (!_socket) {
			ret = NSAPI_ERROR_NO_SOCKET;
			break;
		}

		_pending = 0;
		ret = _stack->socket_send(_socket, data_ptr + written, size - written);
		if (ret >= 0) {
			written += ret;
			if (written >= size) {
				break;
			}
		}
		if (_timeout == 0) {
			break;
		}
		else if (ret == NSAPI_ERROR_WOULD_BLOCK) {
			uint32_t flag;

			// Release lock before blocking so other threads
			// accessing this object aren't blocked
			_lock.unlock();
			flag = _event_flag.wait_any(fd, _timeout);
			_lock.lock();

			if (flag & osFlagsError) {
				// Timeout break
				break;
			}
		}
		else if (ret < 0) {
			break;
		}
	}

	_write_in_progress = false;
	_lock.unlock();
	if (ret <= 0 && ret != NSAPI_ERROR_WOULD_BLOCK) {
		return ret;
	}
	else if (written == 0) {
		return NSAPI_ERROR_WOULD_BLOCK;
	}
	else {
		return written;
	}
}

nsapi_size_or_error_t FilePointer::recv(void *data, nsapi_size_t size)
{
	_lock.lock();
	nsapi_size_or_error_t ret;

	// If this assert is hit then there are two threads
	// performing a recv at the same time which is undefined
	// behavior
	MBED_ASSERT(!_read_in_progress);
	_read_in_progress = true;

	while (true) {
		if (!_socket) {
			ret = NSAPI_ERROR_NO_SOCKET;
			break;
		}

		_pending = 0;
		ret = _stack->socket_recv(_socket, data, size);
		if ((_timeout == 0) || (ret != NSAPI_ERROR_WOULD_BLOCK)) {
			break;
		}
		else {
			uint32_t flag;

			// Release lock before blocking so other threads
			// accessing this object aren't blocked
			_lock.unlock();
			flag = _event_flag.wait_any(fd, _timeout);
			_lock.lock();

			if (flag & osFlagsError) {
				// Timeout break
				ret = NSAPI_ERROR_WOULD_BLOCK;
				break;
			}
		}
	}

	_read_in_progress = false;
	_lock.unlock();
	return ret;
}

nsapi_size_or_error_t FilePointer::sendto(const SocketAddress &address, const void *data, nsapi_size_t size)
{
	_lock.lock();
	nsapi_size_or_error_t ret;

	while (true) {
		if (!_socket) {
			ret = NSAPI_ERROR_NO_SOCKET;
			break;
		}

		_pending = 0;
		nsapi_size_or_error_t sent = _stack->socket_sendto(_socket, address, data, size);
		if ((0 == _timeout) || (NSAPI_ERROR_WOULD_BLOCK != sent)) {
			ret = sent;
			break;
		}
		else {
			uint32_t flag;

			// Release lock before blocking so other threads
			// accessing this object aren't blocked
			_lock.unlock();
			flag = _event_flag.wait_any(fd, _timeout);
			_lock.lock();

			if (flag & osFlagsError) {
				// Timeout break
				ret = NSAPI_ERROR_WOULD_BLOCK;
				break;
			}
		}
	}

	_lock.unlock();
	return ret;
}

nsapi_size_or_error_t FilePointer::recvfrom(SocketAddress *address, void *buffer, nsapi_size_t size)
{
	_lock.lock();
	nsapi_size_or_error_t ret;

	while (true) {
		if (!_socket) {
			ret = NSAPI_ERROR_NO_SOCKET;
			break;
		}

		_pending = 0;
		nsapi_size_or_error_t recv = _stack->socket_recvfrom(_socket, address, buffer, size);
		if ((0 == _timeout) || (NSAPI_ERROR_WOULD_BLOCK != recv)) {
			ret = recv;
			break;
		}
		else {
			uint32_t flag;

			// Release lock before blocking so other threads
			// accessing this object aren't blocked
			_lock.unlock();
			flag = _event_flag.wait_any(fd, _timeout);
			_lock.lock();

			if (flag & osFlagsError) {
				// Timeout break
				ret = NSAPI_ERROR_WOULD_BLOCK;
				break;
			}
		}
	}

	_lock.unlock();
	return ret;
}

nsapi_error_t FilePointer::setsockopt(int level, int optname, const void *optval, unsigned optlen)
{
	_lock.lock();
	nsapi_error_t ret;

	if (!_socket) {
		ret = NSAPI_ERROR_NO_SOCKET;
	}
	else {
		ret = _stack->setsockopt(_socket, level, optname, optval, optlen);
	}

	_lock.unlock();
	return ret;
}

nsapi_error_t FilePointer::getsockopt(int level, int optname, void *optval, unsigned *optlen)
{
	_lock.lock();
	nsapi_error_t ret;

	if (!_socket) {
		ret = NSAPI_ERROR_NO_SOCKET;
	}
	else {
		ret = _stack->getsockopt(_socket, level, optname, optval, optlen);
	}

	_lock.unlock();
	return ret;
}

void FilePointer::event()
{
	if (!in_accept) {
		if (_read_in_progress && (readevt_w == readevt_r)) readevt_w++;
		if (_write_in_progress && (writeevt_w == writeevt_r)) writeevt_w++;
		_event_flag.set(fd);
	}
	else {
		int32_t acount = _accept_sem.wait(0);
		if (acount <= 1) {
			_accept_sem.release();
		}
	}

	_pending += 1;
}

extern "C" int socket(int family, int type, int protocol)
{
	FilePointer *fp;
	nsapi_version_t version;
	unsigned int flags;

	switch (family) {
	case AF_INET:
		version = NSAPI_IPv4;
		break;
	case AF_INET6:
		version = NSAPI_IPv6;
		break;
	default:
		errno = EAFNOSUPPORT;
		return -1;
	}

	flags = type & (SOCK_CLOEXEC | SOCK_NONBLOCK);
	type &= ~flags;

	int fd = new_fd();
	if (fd < 0) {
		errno = ENOMEM;
		return -1;
	}

	switch (type) {
	case SOCK_STREAM:
		fp = new FilePointer(fd, version, NSAPI_TCP, flags);
		break;
	case SOCK_DGRAM:
		fp = new FilePointer(fd, version, NSAPI_UDP, flags);
		break;
	default:
		errno = ENOPROTOOPT;
		return -1;
	}

	auto *net = NetworkInterface::get_default_instance();
	if (net == NULL) {
		delete_fp(fp);
		errno = ENOPROTOOPT;
		return -1;
	}

	auto *stack = nsapi_create_stack(net);
	if (stack == NULL) {
		delete_fp(fp);
		errno = ENOPROTOOPT;
		return -1;
	}

	int ret = fp->open(stack);
	if (ret != NSAPI_ERROR_OK) {
		delete_fp(fp);
		errno = ENOMEM;
		return -1;
	}

	fp_table[fd] = fp;
	return fp->fd + OPEN_MAX;
}

extern "C" int socket_close(int fd)
{
	auto fp = fd_to_fp(fd - OPEN_MAX);
	if (fp == NULL) {
		errno = EBADF;
		return -1;
	}

	fp_table[fd] = NULL;

	auto ret = fp->close();
	if (ret != NSAPI_ERROR_OK) {
		errno = EINVAL;
		return -1;
	}

	return 0;
}

extern "C" int bind(int fd, const struct sockaddr *addr, socklen_t len)
{
	auto fp = fd_to_fp(fd - OPEN_MAX);
	if (fp == NULL) {
		errno = EBADF;
		return -1;
	}

	SocketAddress sockaddr;
	switch (addr->sa_family) {
	case AF_INET:
		if (fp->version != NSAPI_IPv4) {
			errno = EINVAL;
			return -1;
		}
		if (len < sizeof(sockaddr_in)) {
			errno = EINVAL;
			return -1;
		}
		sockaddr.set_ip_bytes(&((sockaddr_in *)addr)->sin_addr, NSAPI_IPv4);
		sockaddr.set_port(ntohs(((sockaddr_in *)addr)->sin_port));
		break;
	case AF_INET6:
		if (fp->version != NSAPI_IPv6) {
			errno = EINVAL;
			return -1;
		}
		if (len < sizeof(sockaddr_in6)) {
			errno = EINVAL;
			return -1;
		}
		sockaddr.set_ip_bytes(&((sockaddr_in6 *)addr)->sin6_addr, NSAPI_IPv6);
		sockaddr.set_port(ntohs(((sockaddr_in6 *)addr)->sin6_port));
		break;
	default:
		errno = EAFNOSUPPORT;
		return -1;
	}

	auto ret = fp->bind(sockaddr);
	if (ret != NSAPI_ERROR_OK) {
		errno = ENOMEM;
		return -1;
	}

	return 0;
}

extern "C" int listen(int fd, int backlog)
{
	auto fp = fd_to_fp(fd - OPEN_MAX);
	if (fp == NULL) {
		errno = EBADF;
		return -1;
	}
	if (fp->protocol != NSAPI_TCP) {
		errno = EINVAL;
		return -1;
	}

	fp->backlog = backlog;

	auto ret = fp->listen(backlog);
	if (ret != NSAPI_ERROR_OK) {
		errno = ENOMEM;
		return -1;
	}

	return 0;
}

extern "C" int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
	auto fp = fd_to_fp(fd - OPEN_MAX);
	if (fp == NULL) {
		errno = EBADF;
		return -1;
	}
	if (fp->protocol != NSAPI_TCP) {
		errno = EINVAL;
		return -1;
	}

	SocketAddress sockaddr;
	switch (addr->sa_family) {
	case AF_INET:
		if (fp->version != NSAPI_IPv4) {
			errno = EINVAL;
			return -1;
		}
		if (len < sizeof(sockaddr_in)) {
			errno = EINVAL;
			return -1;
		}
		sockaddr.set_ip_bytes(&((sockaddr_in *)addr)->sin_addr, NSAPI_IPv4);
		sockaddr.set_port(ntohs(((sockaddr_in *)addr)->sin_port));
		break;
	case AF_INET6:
		if (fp->version != NSAPI_IPv6) {
			errno = EINVAL;
			return -1;
		}
		if (len < sizeof(sockaddr_in6)) {
			errno = EINVAL;
			return -1;
		}
		sockaddr.set_ip_bytes(&((sockaddr_in6 *)addr)->sin6_addr, NSAPI_IPv6);
		sockaddr.set_port(ntohs(((sockaddr_in6 *)addr)->sin6_port));
		break;
	default:
		errno = EAFNOSUPPORT;
		return -1;
	}

	auto ret = fp->connect(sockaddr);
	if (ret != NSAPI_ERROR_OK) {
		errno = ENOMEM;
		return -1;
	}

	return 0;
}

extern "C" int accept(int fd, struct sockaddr *__restrict addr, socklen_t *__restrict len)
{
	auto fp = fd_to_fp(fd - OPEN_MAX);
	if (fp == NULL) {
		errno = EBADF;
		return -1;
	}
	if (fp->protocol != NSAPI_TCP) {
		errno = EINVAL;
		return -1;
	}

	FilePointer *conn;

	int cfd = new_fd();
	if (cfd < 0) {
		errno = ENOMEM;
		return -1;
	}

	switch (fp->version) {
	case NSAPI_IPv4:
	{
		if ((len != NULL) && (*len < sizeof(sockaddr_in))) {
			errno = EINVAL;
			return -1;
		}
		conn = new FilePointer(cfd, NSAPI_IPv4, NSAPI_TCP, 0);
		break;
	}
	case NSAPI_IPv6:
	{
		if ((len != NULL) && (*len < sizeof(sockaddr_in6))) {
			errno = EINVAL;
			return -1;
		}
		conn = new FilePointer(cfd, NSAPI_IPv6, NSAPI_TCP, 0);
		break;
	}
	default:
		errno = EINVAL;
		return -1;
	}
	fp_table[cfd] = conn;

	auto ret = conn->open(fp->get_stack());
	if (ret != NSAPI_ERROR_OK) {
		errno = ENOMEM;
		return -1;
	}

	SocketAddress sockaddr;
	ret = fp->accept(conn, &sockaddr);
	if (ret != NSAPI_ERROR_OK) {
		errno = ENOMEM;
		return -1;
	}

	if (addr != NULL && len != NULL) {
		switch (sockaddr.get_ip_version()) {
		case NSAPI_IPv4:
		{
			struct sockaddr_in *raddr = (struct sockaddr_in *)addr;
			raddr->sin_family = AF_INET;
			raddr->sin_port = htons(sockaddr.get_port());
			memcpy(&raddr->sin_addr, sockaddr.get_ip_bytes(), 4);
			break;
		}
		case NSAPI_IPv6:
		{
			struct sockaddr_in6 *raddr = (struct sockaddr_in6 *)addr;
			raddr->sin6_family = AF_INET6;
			raddr->sin6_port = htons(sockaddr.get_port());
			memcpy(&raddr->sin6_addr, sockaddr.get_ip_bytes(), 16);
			break;
		}
		}
	}

	return fp->fd + OPEN_MAX;
}

extern "C" ssize_t send(int fd, const void *buf, size_t len, int flags)
{
	return sendto(fd, buf, len, flags, NULL, 0);
}

extern "C" ssize_t sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *addr, socklen_t alen)
{
	auto fp = fd_to_fp(fd - OPEN_MAX);
	if (fp == NULL) {
		errno = EBADF;
		return -1;
	}

	int ret = 0;
	switch (fp->protocol) {
	case NSAPI_TCP:
		if ((addr != NULL) || (alen != 0)) {
			errno = EISCONN;
			return -1;
		}
		ret = fp->send(buf, len);
		break;
	case NSAPI_UDP:
		if ((addr == NULL) || (alen == 0)) {
			errno = EINVAL;
			return -1;
		}

		SocketAddress sockaddr;
		switch (addr->sa_family) {
		case AF_INET:
			if (fp->version != NSAPI_IPv4) {
				errno = EINVAL;
				return -1;
			}
			if (len < sizeof(sockaddr_in)) {
				errno = EINVAL;
				return -1;
			}
			sockaddr.set_ip_bytes(&((sockaddr_in *)addr)->sin_addr, NSAPI_IPv4);
			sockaddr.set_port(ntohs(((sockaddr_in *)addr)->sin_port));
			break;
		case AF_INET6:
			if (fp->version != NSAPI_IPv6) {
				errno = EINVAL;
				return -1;
			}
			if (len < sizeof(sockaddr_in6)) {
				errno = EINVAL;
				return -1;
			}
			sockaddr.set_ip_bytes(&((sockaddr_in6 *)addr)->sin6_addr, NSAPI_IPv6);
			sockaddr.set_port(ntohs(((sockaddr_in6 *)addr)->sin6_port));
			break;
		default:
			errno = EAFNOSUPPORT;
			return -1;
		}

		ret = fp->sendto(sockaddr, buf, len);
		break;
	}

	if (ret < 0) {
		errno = ECOMM;
		return -1;
	}

	return ret;
}

extern "C" ssize_t recv(int fd, void *buf, size_t len, int flags)
{
	return recvfrom(fd, buf, len, flags, NULL, 0);
}

extern "C" ssize_t recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *__restrict addr, socklen_t *__restrict alen)
{
	auto fp = fd_to_fp(fd - OPEN_MAX);
	if (fp == NULL) {
		errno = EBADF;
		return -1;
	}

	if (addr != NULL && len != 0) {
		switch (fp->version) {
		case NSAPI_IPv4:
		{
			if (len < sizeof(sockaddr_in)) {
				errno = EINVAL;
				return -1;
			}
			break;
		}
		case NSAPI_IPv6:
		{
			if (len < sizeof(sockaddr_in6)) {
				errno = EINVAL;
				return -1;
			}
			break;
		}
		default:
			errno = EINVAL;
			return -1;
		}
	}

	int ret = 0;
	switch (fp->protocol) {
	case NSAPI_TCP:
		ret = fp->recv(buf, len);
		if (ret < 0) {
			errno = ECOMM;
			return -1;
		}

		break;
	case NSAPI_UDP:
		SocketAddress sockaddr;
		ret = fp->recvfrom(&sockaddr, buf, len);
		if (ret < 0) {
			errno = ECOMM;
			return -1;
		}

		if (addr != NULL && len != 0) {
			switch (sockaddr.get_ip_version()) {
			case NSAPI_IPv4:
			{
				struct sockaddr_in *raddr = (struct sockaddr_in *)addr;
				raddr->sin_family = AF_INET;
				raddr->sin_port = htons(sockaddr.get_port());
				memcpy(&raddr->sin_addr, sockaddr.get_ip_bytes(), 4);
				break;
			}
			case NSAPI_IPv6:
			{
				struct sockaddr_in6 *raddr = (struct sockaddr_in6 *)addr;
				raddr->sin6_family = AF_INET6;
				raddr->sin6_port = htons(sockaddr.get_port());
				memcpy(&raddr->sin6_addr, sockaddr.get_ip_bytes(), 16);
				break;
			}
			}
		}
		break;
	}

	return ret;
}

extern "C" int getsockopt(int fd, int level, int optname, void *optval, socklen_t *__restrict optlen)
{
	auto fp = fd_to_fp(fd - OPEN_MAX);
	if (fp == NULL) {
		errno = EBADF;
		return -1;
	}

	if ((level == SOL_SOCKET) && (optname == SO_ERROR)) {
		auto ret = fp->getsockopt(NSAPI_SOCKET, NSAPI_ERROR, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
	else if ((level == SOL_SOCKET) && (optname == SO_KEEPALIVE)) {
		auto ret = fp->getsockopt(NSAPI_SOCKET, NSAPI_SO_KEEPALIVE, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
	else if ((level == SOL_SOCKET) && (optname == SO_RCVTIMEO)) {
		auto ret = fp->getsockopt(NSAPI_SOCKET, NSAPI_SO_RCVTIMEO, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
	else if ((level == SOL_SOCKET) && (optname == SO_REUSEADDR)) {
		auto ret = fp->getsockopt(NSAPI_SOCKET, NSAPI_REUSEADDR, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
#ifdef TCP_KEEPALIVE
	else if ((level == IPPROTO_TCP) && (optname == TCP_KEEPALIVE)) {
		auto ret = fp->getsockopt(NSAPI_SOCKET, NSAPI_KEEPALIVE, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
#endif
#ifdef TCP_KEEPIDLE
	else if ((level == IPPROTO_TCP) && (optname == TCP_KEEPIDLE)) {
		auto ret = fp->getsockopt(NSAPI_SOCKET, NSAPI_KEEPIDLE, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
#endif
#ifdef TCP_KEEPINTVL
	else if ((level == IPPROTO_TCP) && (optname == TCP_KEEPINTVL)) {
		auto ret = fp->getsockopt(NSAPI_SOCKET, NSAPI_KEEPINTVL, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
#endif
	else if ((level == IPPROTO_TCP) && (optname == TCP_NODELAY)) {
		auto ret = fp->getsockopt(NSAPI_SOCKET, NSAPI_NODELAY, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
	else
		return -EINVAL;

	return 0;
}

extern "C" int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
	auto fp = fd_to_fp(fd - OPEN_MAX);
	if (fp == NULL) {
		errno = EBADF;
		return -1;
	}

	if ((level == IPPROTO_IP) && (optname == IP_ADD_MEMBERSHIP)) {
		const struct ip_mreq *imr = (const struct ip_mreq *)optval;
		if (optlen < sizeof(struct ip_mreq))
			return -EINVAL;

		nsapi_ip_mreq_t mreq;
		mreq.imr_interface.version = NSAPI_IPv4;
		memcpy(mreq.imr_interface.bytes, &imr->imr_interface, sizeof(imr->imr_interface));
		mreq.imr_multiaddr.version = NSAPI_IPv4;
		memcpy(mreq.imr_multiaddr.bytes, &imr->imr_multiaddr, sizeof(imr->imr_multiaddr));

		auto ret = fp->setsockopt(NSAPI_SOCKET, NSAPI_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
	else if ((level == IPPROTO_IP) && (optname == IP_DROP_MEMBERSHIP)) {
		const struct ip_mreq *imr = (const struct ip_mreq *)optval;
		if (optlen < sizeof(struct ip_mreq))
			return -EINVAL;

		nsapi_ip_mreq_t mreq;
		mreq.imr_interface.version = NSAPI_IPv4;
		memcpy(mreq.imr_interface.bytes, &imr->imr_interface, sizeof(imr->imr_interface));
		mreq.imr_multiaddr.version = NSAPI_IPv4;
		memcpy(mreq.imr_multiaddr.bytes, &imr->imr_multiaddr, sizeof(imr->imr_multiaddr));

		auto ret = fp->setsockopt(NSAPI_SOCKET, NSAPI_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
#ifdef TCP_KEEPALIVE
	else if ((level == IPPROTO_TCP) && (optname == TCP_KEEPALIVE)) {
		auto ret = fp->setsockopt(NSAPI_SOCKET, NSAPI_KEEPALIVE, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
#endif
#ifdef TCP_KEEPALIVE
	else if ((level == IPPROTO_TCP) && (optname == TCP_KEEPIDLE)) {
		auto ret = fp->setsockopt(NSAPI_SOCKET, NSAPI_KEEPIDLE, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
#endif
#ifdef TCP_KEEPALIVE
	else if ((level == IPPROTO_TCP) && (optname == TCP_KEEPINTVL)) {
		auto ret = fp->setsockopt(NSAPI_SOCKET, NSAPI_KEEPINTVL, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
#endif
	else if ((level == IPPROTO_TCP) && (optname == TCP_NODELAY)) {
		auto ret = fp->setsockopt(NSAPI_SOCKET, NSAPI_NODELAY, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
	else if ((level == SOL_SOCKET) && (optname == SO_KEEPALIVE)) {
		auto ret = fp->setsockopt(NSAPI_SOCKET, NSAPI_SO_KEEPALIVE, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
	else if ((level == SOL_SOCKET) && (optname == SO_RCVTIMEO)) {
		auto ret = fp->setsockopt(NSAPI_SOCKET, NSAPI_SO_RCVTIMEO, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
	else if ((level == SOL_SOCKET) && (optname == SO_REUSEADDR)) {
		auto ret = fp->setsockopt(NSAPI_SOCKET, NSAPI_REUSEADDR, optval, optlen);
		if (ret != NSAPI_ERROR_OK)
			return -EINVAL;
	}
	else
		return -EINVAL;

	return 0;
}

extern "C" unsigned int if_nametoindex(const char *ifname)
{
	return 1;
}

char *host_aliases[1] = { 0 };
char host_address[16];
char *host_addresses[2] = { host_address, 0 };
struct hostent h_addr_list[1];

extern "C" struct hostent *gethostbyname(const char *host)
{
	SocketAddress address;

	auto *net = NetworkInterface::get_default_instance();
	if (net == NULL)
		return NULL;

	if (net->gethostbyname(host, &address) != NSAPI_ERROR_OK)
		return NULL;

	h_addr_list[0].h_name = (char *)host;
	h_addr_list[0].h_aliases = host_aliases;
	switch (address.get_ip_version()) {
	case NSAPI_IPv4:
		h_addr_list[0].h_addrtype = AF_INET;
		h_addr_list[0].h_length = 4;
		break;
	case NSAPI_IPv6:
		h_addr_list[0].h_addrtype = AF_INET6;
		h_addr_list[0].h_length = 16;
		break;
	default:
		return NULL;
	}
	h_addr_list[0].h_addr_list = host_addresses;

	memcpy(host_address, address.get_ip_bytes(), h_addr_list[0].h_length);

	return h_addr_list;
}

extern "C" int getpeername(int fd, struct sockaddr *__restrict addr, socklen_t *__restrict len)
{
	auto fp = fd_to_fp(fd - OPEN_MAX);
	if (fp == NULL) {
		errno = EBADF;
		return -1;
	}
	if (len == NULL) {
		errno = EINVAL;
		return -1;
	}

	socklen_t size = *len;
	switch (fp->version) {
	case NSAPI_IPv4:
	{
		struct sockaddr_in *raddr = &fp->raddr4;
		*len = sizeof(struct sockaddr_in);
		if (size > sizeof(struct sockaddr_in))
			size = sizeof(struct sockaddr_in);
		memcpy(addr, raddr, size);
		break;
	}
	case NSAPI_IPv6:
	{
		struct sockaddr_in6 *raddr = &fp->raddr6;
		*len = sizeof(struct sockaddr_in6);
		if (size > sizeof(struct sockaddr_in6))
			size = sizeof(struct sockaddr_in6);
		memcpy(addr, raddr, size);
		break;
	}
	}

	return 0;
}

extern "C" int getsockname(int fd, struct sockaddr *__restrict addr, socklen_t *__restrict len)
{
	auto fp = fd_to_fp(fd - OPEN_MAX);
	if (fp == NULL) {
		errno = EBADF;
		return -1;
	}
	if (len == NULL) {
		errno = EINVAL;
		return -1;
	}

	socklen_t size = *len;
	switch (fp->version) {
	case NSAPI_IPv4:
	{
		SocketAddress sockaddr;
		fp->get_ip_address(&sockaddr);
		struct sockaddr_in laddr;
		memset(&laddr, 0, sizeof(laddr));
		laddr.sin_family = AF_INET;
		laddr.sin_port = htons(sockaddr.get_port());
		memcpy(&laddr.sin_addr, sockaddr.get_ip_bytes(), 4);
		*len = sizeof(struct sockaddr_in);
		if (size > sizeof(struct sockaddr_in))
			size = sizeof(struct sockaddr_in);
		memcpy(addr, &laddr, size);
		break;
	}
	case NSAPI_IPv6:
	{
		SocketAddress sockaddr;
		fp->get_ip_address(&sockaddr);
		struct sockaddr_in6 laddr;
		memset(&laddr, 0, sizeof(laddr));
		laddr.sin6_family = AF_INET6;
		laddr.sin6_port = htons(sockaddr.get_port());
		memcpy(&laddr.sin6_addr, sockaddr.get_ip_bytes(), 16);
		*len = sizeof(struct sockaddr_in);
		if (size > sizeof(struct sockaddr_in))
			size = sizeof(struct sockaddr_in);
		memcpy(addr, &laddr, size);
		break;
	}
	}

	return 0;
}

void memand(void *dst, void *src, size_t len)
{
	uint8_t *d = (uint8_t *)dst;
	uint8_t *s = (uint8_t *)src;
	uint8_t *e = &s[len];

	while (s < e) {
		*d++ &= *s++;
	}
}

struct fd_events {
	int count;
	fd_set readfds;
	fd_set writefds;
	fd_set errorfds;
};

uint32_t get_evts(struct fd_events *evts, uint32_t tmout);

#define TMO_MAX INT_MAX

extern "C" int select(int n, fd_set *__restrict rfds, fd_set *__restrict wfds, fd_set *__restrict efds, struct timeval *__restrict tv)
{
	uint32_t ret;
	uint32_t tmout = osWaitForever;
	struct fd_events evts;

	if (tv != NULL) {
		if (tv->tv_sec < (TMO_MAX / 1000000))
			tmout = tv->tv_sec * 1000000 + tv->tv_usec;
		else
			tmout = TMO_MAX;
	}

	if (rfds != NULL)
		memcpy(&evts.readfds, rfds, sizeof(fd_set));
	else
		memset(&evts.readfds, 0, sizeof(fd_set));
	if (wfds != NULL)
		memcpy(&evts.writefds, wfds, sizeof(fd_set));
	else
		memset(&evts.writefds, 0, sizeof(fd_set));
	if (efds != NULL)
		memcpy(&evts.errorfds, efds, sizeof(fd_set));
	else
		memset(&evts.errorfds, 0, sizeof(fd_set));
	evts.count = 0;

	ret = get_evts(&evts, tmout);
	if (ret == 0) {
		if (rfds != NULL)
			memand(rfds, &evts.readfds, sizeof(fd_set));
		if (wfds != NULL)
			memand(wfds, &evts.writefds, sizeof(fd_set));
		if (efds != NULL)
			memand(efds, &evts.errorfds, sizeof(fd_set));
		return evts.count;
	}
	if (ret == osFlagsErrorTimeout) {
		if (rfds != NULL)
			memset(rfds, 0, sizeof(fd_set));
		if (wfds != NULL)
			memset(wfds, 0, sizeof(fd_set));
		if (efds != NULL)
			memset(efds, 0, sizeof(fd_set));
		return 0;
	}

	errno = EBADF;
	return -1;
}

extern "C" int socket_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	uint32_t ret;
	uint32_t tmout;
	struct fd_events evts;

	if (timeout < 0)
		tmout = osWaitForever;
	else if (timeout < (TMO_MAX / 1000))
		tmout = timeout * 1000;
	else
		tmout = TMO_MAX;

	memset(&evts, 0, sizeof(evts));

	for (int i = 0; i < nfds; i++) {
		struct pollfd *pfd = &fds[i];
		int fd = pfd->fd;
		if ((fd < 0) || (fd >= fp_table.size()))
			continue;

		if (pfd->events & POLLIN)
			FD_SET(fd, &evts.readfds);
		if (pfd->events & POLLOUT)
			FD_SET(fd, &evts.writefds);
		if (pfd->events & POLLERR)
			FD_SET(fd, &evts.errorfds);
		pfd->revents = 0;
	}

	ret = get_evts(&evts, tmout);
	if (ret == 0) {
		int result = 0;
		for (int i = 0; i < nfds; i++) {
			struct pollfd *pfd = &fds[i];
			int fd = pfd->fd;
			if ((fd < 0) || (fd >= fp_table.size()))
				continue;

			if (FD_ISSET(fd, &evts.readfds))
				pfd->revents |= POLLIN;
			if (FD_ISSET(fd, &evts.writefds))
				pfd->revents |= POLLOUT;
			if (FD_ISSET(fd, &evts.errorfds))
				pfd->revents |= POLLERR;
			if (pfd->revents != 0)
				result++;
		}
		return result;
	}
	if (ret == osFlagsErrorTimeout) {
		return 0;
	}

	errno = EBADF;
	return -1;
}

uint32_t get_evts(struct fd_events *evts, uint32_t tmout)
{
	uint32_t ret;
	uint32_t waitptn, flgptn = 0, readfds = 0, writefds = 0;
	FilePointer *fp = NULL;
	int count = 0;

	waitptn = *((uint32_t *)&evts->readfds) | *((uint32_t *)&evts->errorfds);
	for (auto fp : fp_table) {
		if (fp == NULL)
			continue;

		if (FD_ISSET(fp->fd, &evts->writefds)) {
			if (fp->writeevt_w == fp->writeevt_r) {
				FD_SET(fp->fd, (fd_set *)&writefds);
				count++;
				if (fp->writeevt_w == fp->writeevt_r) fp->writeevt_r--;
			}
			else {
				FD_SET(fp->fd, (fd_set *)&waitptn);
			}
		}
	}
	memset(evts, 0, sizeof(*evts));

	if (waitptn == 0) {
		memcpy(&evts->readfds, &readfds, sizeof(evts->readfds));
		memcpy(&evts->writefds, &writefds, sizeof(evts->writefds));
		evts->count = count;
		return 0;
	}
	else if ((readfds | writefds) != 0) {
		_event_flag.set(readfds | writefds);
	}

	flgptn = _event_flag.wait_any(waitptn, tmout, false);
	if ((flgptn & osFlagsError) != 0) {
		if (flgptn != osFlagsErrorTimeout) {
			return flgptn;
		}

		if (flgptn == 0)
			return osFlagsErrorTimeout;
	}
	flgptn &= waitptn;

	ret = _event_flag.clear(~flgptn);
	if (ret != 0) {
		MBED_ASSERT(ret == 0);
	}

	count = 0;
	for (auto fp : fp_table) {
		if (fp == NULL)
			continue;

		if (!FD_ISSET(fp->fd, (fd_set *)&waitptn))
			continue;

		if (fp->readevt_w != fp->readevt_r) {
			fp->readevt_r++;
			FD_SET(fp->fd, &evts->readfds);
			count++;
		}
		if (fp->writeevt_w != fp->writeevt_r) {
			fp->writeevt_r++;
			FD_SET(fp->fd, &evts->writefds);
			count++;
		}
		if (fp->errorevt_w != fp->errorevt_r) {
			fp->errorevt_r++;
			FD_SET(fp->fd, &evts->errorfds);
			count++;
		}
	}
	evts->count = count;

	return 0;
}
