// @@@LICENSE
//
//      Copyright (c) 2009-2012 Hewlett-Packard Development Company, L.P.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// LICENSE@@@

#include <stdio.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <syslog.h>
#include <stdarg.h>

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <tr1/unordered_map>

#include <boost/program_options.hpp>
#include <boost/throw_exception.hpp>

#include <pbnjson.hpp>

#ifndef _GNU_SOURCE
#define POLLRDHUP 0
#endif

namespace po = boost::program_options;

class FDHandler;

static int gPort;
static std::string gHost;
static bool gRunAsDaemon = false;
static bool gUseSyslog = false;
static bool gNoStdin = false;
static bool gNoChildStdout = false;
static bool gNoChildStderr = false;
static bool gFinished = false;
static int gExitCode = 0;
static int64_t gMainEnterTime = 0;

typedef std::vector<std::string> StrVector;
typedef std::vector<struct pollfd> PollVector;
typedef std::vector<char> ByteArray;
typedef std::tr1::unordered_map<int, FDHandler *> PollLookup;

static void log(const char *format, ...)
{
	va_list vargs;
	va_start(vargs, format);
	if (gUseSyslog) {
		vsyslog(LOG_NOTICE, format, vargs);
	} else {
		vfprintf(stderr, format, vargs);
	}
	va_end(vargs);
}

static int uptime()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	int64_t now = ts.tv_sec * 1000000000 + ts.tv_nsec;
	return now - gMainEnterTime;
}

static int safe_write(int fd, const void *buf, size_t count)
{
	size_t len = count;
	int ret = 0;
	while (len) {
		ret = ::write(fd, (char*)buf + (count - len), len);

		if (ret < 0) {
			return ret;
		}

		len -= ret;
	}

	return 0;
}

static void stopApplication(int exitCode)
{
	gFinished = true;
	gExitCode = exitCode;
}

static std::string ErrPrefix(bool newline)
{
	static std::string errPrefix = "******* ";
	static std::string errPrefixNewline = "\n" + errPrefix;
	return newline ? errPrefixNewline : errPrefix;
}

static std::ostream& operator<<(std::ostream& o, const StrVector& v)
{
	o << "[";
	for (int i = 0, ni = v.size(); i < ni; i++) {
		o << '\'' << v[i] << '\'';
		if (i != ni - 1)
			o << ", ";
	}
	return o << "]";
}

struct AddressInfo {
	AddressInfo() : adresses(NULL) {}
	~AddressInfo()
	{
		if (adresses)
			freeaddrinfo(adresses);
	}

	int init(const std::string host, const std::string port, const struct addrinfo *hints)
	{
		int err;
		if ((err = getaddrinfo(host.empty() ? NULL : host.c_str(), port.c_str(), hints, &adresses)) != 0) {
			adresses = NULL;
		}
		return err;
	}

	struct addrinfo *adresses;
};

static int launch(int port, const std::string& host, const std::string& script, const StrVector& args)
{
	std::ostringstream portStrWriter;
	std::string portStr;
	struct addrinfo hints;
	AddressInfo resolved;
	int err;

	portStrWriter << port;
	portStr = portStrWriter.str();

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags |= AI_NUMERICSERV;

	if ((err = resolved.init(host, portStr, &hints)) != 0) {
		std::string msg = gai_strerror(err);
		if (err == EAI_SYSTEM)
			msg += ": " + std::string(strerror(errno));
		std::cerr << ErrPrefix(false) << "Failed to resolve " << host << ":" << port << " :: " << msg << "\n";
		return -1;
	}

	struct addrinfo *address = resolved.adresses;
	for (struct addrinfo *address = resolved.adresses; address; address = address->ai_next) {
#ifndef NDEBUG
		std::cerr << ErrPrefix(false) << "Trying next server address..." << "\n";
#endif
		int sfd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
		if (sfd == -1)
			continue;

		if (-1 == connect(sfd, address->ai_addr, address->ai_addrlen)) {
			if (errno != EINPROGRESS) {
				close(sfd);
				continue;
			}
		}

		return sfd;
	}

	std::cerr << ErrPrefix(false) << "Failed to connect to host" << "\n";
	return -1;
}

static volatile int sigcnt_INT = 0;
static volatile int sigcnt_TERM = 0;

static void sh_int(int signum)
{
	sigcnt_INT++;
	signal(SIGINT, sh_int);
}

static void sh_term(int signum)
{
	std::cerr << "SIGTERM" << std::endl;
	sigcnt_TERM++;
	signal(SIGTERM, sh_term);
}

static void sh_usr(int signum)
{
	if (signum == SIGUSR1)
		signal(SIGUSR1, sh_usr);
	else
		signal(SIGUSR2, sh_usr);
}

static void sh_alarm(int signum)
{
	signal(SIGALRM, sh_alarm);
}

typedef void (*reader)(int fd);
typedef void (*writer)(int fd);

static FDHandler *gStdinHandler = NULL;
static FDHandler *gChildSocketHandler = NULL;

struct FDHandlerInfo {
	struct pollfd pollInfo;
	FDHandler *handler;
};

class FDHandler {
public:
	FDHandler(int fd)
		: m_target(NULL)
	{
		m_fd = fd;
		int flags = fcntl(m_fd, F_GETFL, 0);
		if (fcntl(m_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
			std::cerr << ErrPrefix(false) << "Failed to make descriptor non-blocking";
		}
		m_readBuffer.resize(4096, 0);
	}

	~FDHandler()
	{
		if (isConnected())
			closed();
	}

	void redirectInput(FDHandler *output)
	{
		m_target = output;
	}

	struct pollfd pollInfo(int events) const
	{
		struct pollfd info;
		info.fd = m_fd;
		info.events = events;
		info.revents = 0;
		return info;
	}

	virtual bool knowConnectionState()
	{
		return true;
	}

	virtual bool isConnected()
	{
		return m_fd != -1;
	}

	void read()
	{
		ssize_t readSize = ::read(m_fd, m_readBuffer.data(), m_readBuffer.capacity());
		if (m_pending.empty()) {
			m_pending = m_readBuffer;
			m_pending.resize(readSize);
		}
		else {
			size_t oldSize = m_pending.size();
			m_pending.resize(m_pending.size() + readSize, 0);
			memcpy(m_pending.data() + oldSize, m_readBuffer.data(), readSize);
		}
		processMessages();
	}

	virtual void connected();
	virtual void error();

	void closed()
	{
		processClosed();
		close(m_fd);
		m_fd = -1;
	}

	bool hasPendingWrite() const
	{
		return !m_writeBuffer.empty();
	}

	bool flush() {
		if (!m_writeBuffer.empty()) {
			ssize_t written = ::write(m_fd, m_writeBuffer.data(), m_writeBuffer.size());
			if (written > 0 && written <= m_writeBuffer.size()) {
				int new_len = m_writeBuffer.end() - m_writeBuffer.begin() + written;
				try {
					m_writeBuffer = ByteArray(m_writeBuffer.begin() + written, m_writeBuffer.end());
				} catch (const std::exception& e) {
					log("ByteArray creation of size %d failed at line: %d with exception: %s\n", new_len,  __LINE__, e.what());
					throw e;
				}
			}
		}
		return m_writeBuffer.empty();
	}

	virtual void sendMsg(const void *buf, size_t length)
	{
		write(buf, length);
	}

protected:
	virtual void processMessages()
	{
		message(&m_pending[0], m_pending.size());
		m_pending.clear();
	}

	void write(const void *buf, size_t len)
	{
		if (!flush()) {
			size_t oldSize = m_writeBuffer.size();
			m_writeBuffer.resize(oldSize + len, 0);
			memcpy(m_writeBuffer.data() + oldSize, buf, len);
		} else {
			ssize_t written = ::write(m_fd, buf, len);
			if (written != len) {
				try {
					if (written < 0) {
						log("%s: write failed, fd: %d, len: %d\n", __PRETTY_FUNCTION__, m_fd, len);
						written = 0;
					}
					m_writeBuffer = ByteArray(static_cast<const char *>(buf) + written, static_cast<const char *>(buf) + len - written);
				} catch (const std::exception& e) {
					log("ByteArray creation of size %d failed at line: %d with exception: %s\n", len,  __LINE__, e.what());
					throw e;
				}
			}
		}
	}

	virtual void message(const void *start, size_t length)
	{
		if (m_target) {
			m_target->sendMsg(start, length);
		}
	}

	virtual void processError() {}
	virtual void processClosed() {}

	ByteArray m_pending;

	int fd()
	{
		return m_fd;
	}

private:
	int m_fd;
	ByteArray m_readBuffer;
	ByteArray m_writeBuffer;
	FDHandler *m_target;
};

void FDHandler::connected()
{
	std::cerr << "FDHandler " << this << " connected\n";
}

void FDHandler::error()
{
	closed();
}

class JSONSocketHandler : public FDHandler {
public:
	JSONSocketHandler(int fd)
		: FDHandler(fd)
		, m_pendingOffset(0)
		, determined_connectability(false)
	{
	}

	virtual void sendMsg(pbnjson::JValue msg)
	{
		std::string serialized;
		pbnjson::JGenerator serializer;
		if (serializer.toString(msg, pbnjson::JSchemaFragment("{}"), serialized)) {
			// null character is implicit in stl string
			write(serialized.c_str(), serialized.size() + 1);
		}
	}

	virtual void connected() = 0;

	bool knowConnectionState() const
	{
		return determined_connectability;
	}

	bool isConnected()
	{
		if (!FDHandler::isConnected())
			return false;

		if (knowConnectionState())
			return connectionEstablished;

		int err;
		socklen_t errlen = sizeof(err);
		if (0 == getsockopt(fd(), SOL_SOCKET, SO_ERROR, &err, &errlen)) {
			determined_connectability = true;
			connectionEstablished = err == 0;
			if (!connectionEstablished) {
				closed();
				std::cerr << ErrPrefix(true) << strerror(err) << "\n";
			} else {
//				std::cerr << "JSONSocketHandler " << this << " connected\n";
				connected();
			}
		}
		return determined_connectability && connectionEstablished;
	}

protected:
	virtual void processMessages()
	{
		size_t messageStart = m_pendingOffset;
		for (size_t i = m_pendingOffset, ni = m_pending.size(); i < ni; i++) {
			if (m_pending[i] == 0) {
				message(&m_pending[messageStart], i - messageStart);
				messageStart = i + 1;
			}
		}

		if (messageStart == m_pending.size()) {
			m_pendingOffset = 0;
			m_pending.clear();
		} else {
			m_pendingOffset = messageStart;
		}

		if (m_pendingOffset >= 4096) {
			try {
				m_pending = ByteArray(m_pending.begin() + m_pendingOffset, m_pending.end());
			} catch (const std::exception& e) {
				log("ByteArray creation of size %d failed at line: %d with exception: %s\n", m_pending.end() - (m_pending.begin() + m_pendingOffset),  __LINE__, e.what());
                                throw e;
			}
			m_pendingOffset = 0;
		}
	}

	virtual void message(const void *start, size_t length)
	{
		std::string input(static_cast<const char *>(start), length);
		pbnjson::JDomParser parser;
		if (!parser.parse(input, pbnjson::JSchemaFragment("{}"))) {
			std::cerr << ErrPrefix(true) << "Malformed message: " << input;
			return;
		}

		jsonMessage(input, parser.getDom());
	}

	virtual void jsonMessage(const std::string& msgStr, pbnjson::JValue msg) = 0;

private:
	size_t m_pendingOffset;
	bool determined_connectability;
	bool connectionEstablished;
};

class SpawnSocketHandler : public JSONSocketHandler {
public:
	SpawnSocketHandler(const std::string& script, const StrVector& args, int fd)
		: JSONSocketHandler(fd)
		, m_script(script)
		, m_args(args)
		, m_pid(-1)
	{
		isConnected();
	}

	virtual void connected()
	{
		// send spawn request
		pbnjson::JValue spawnRequest = pbnjson::Object();
		spawnRequest.put("spawn", true);
		spawnRequest.put("script", m_script);
		spawnRequest.put("childstdout", !gNoChildStdout);
		spawnRequest.put("childstderr", !gNoChildStderr);
		if (!m_args.empty()) {
			spawnRequest.put("args", pbnjson::Array());
			for (size_t i = 0, ni = m_args.size(); i < ni; i++) {
				spawnRequest["args"].append(m_args.at(i));
			}
		}
		JSONSocketHandler::sendMsg(spawnRequest);
	}

	int childPid() const
	{
		return m_pid;
	}

	bool spawned() const
	{
		return m_pid != -1;
	}

protected:
	void sendMsg(const void *buf, size_t len)
	{
		pbnjson::JValue msg = pbnjson::Object();
		msg.put("stdin", std::string(static_cast<const char *>(buf), len));
		JSONSocketHandler::sendMsg(msg);
	}

	void jsonMessage(const std::string& msgStr, pbnjson::JValue msg)
	{
		bool handled = false;

		if (!msg.isObject()) {
			std::cerr << ErrPrefix(true) << "Unexpected message from server: '" << msgStr << "'" << "\n";
			return;
		}

		if (msg["spawned"].isBoolean()) {
			if (!msg["spawned"].asBool()) {
				std::cerr << "Failed to spawn " << m_script << ": " << msg["errMsg"].asString() << "\n";
				stopApplication(4);
				return;
			}
			int m_pid = msg["pid"].asNumber<int>();
			if (m_pid < 0) {
				std::cerr << "Invalid pid returned" << std::endl;
				stopApplication(-1);
				return;
			}

			log("[%.3lf] Got confirmation from server that child has initialized\n", (uptime()) / 1000000.0);

			if (gStdinHandler && !gRunAsDaemon)
				gStdinHandler->redirectInput(this);
			else if (gRunAsDaemon) {
				stopApplication(0);
				return;
			}
			handled = true;
		}

		std::string output;
		if (CONV_OK == msg["stdout"].asString(output)) {
			if (getenv("TIMESTAMP_OUTPUT"))
				fprintf(stdout, "[%.3lf] %s", uptime() / 1000000.0, output.c_str());
			else
				safe_write(STDOUT_FILENO, output.c_str(), output.size());
			handled = true;
		}
		if (CONV_OK == msg["stderr"].asString(output)) {
			if (getenv("TIMESTAMP_OUTPUT"))
				fprintf(stderr, "[%.3lf] %s", uptime() / 1000000.0, output.c_str());
			else
				safe_write(STDERR_FILENO, output.c_str(), output.size());
			handled = true;
		}

		if (msg["dead"].asBool()) {
			int exitCode;
			int signum = -1;
			if (CONV_OK != msg["exitCode"].asNumber(exitCode))
				exitCode = 0;
			if (CONV_OK == msg["signal"].asNumber(signum)) {
				std::cerr << ErrPrefix(true) << "Child died from signal: " << signum << std::endl;
			}
			if (signum != -1 && exitCode == 0) {
				std::cerr << ErrPrefix(true) << "Child died from signal but exitCode was marked as 0 - adjusting so that cause of death is correct" << std::endl;
				exitCode = signum == 0 ? -1 : signum;
			}
			stopApplication(exitCode);
			handled = true;
		}

		if (!handled) {
			log("Unhandled message: %s", msgStr.c_str());
		}
	}

private:
	std::string m_script;
	StrVector m_args;
	int m_pid;
};

static void addFDListener(PollVector &pollList, PollLookup &pollLookup, FDHandler *handler, int events)
{
	if (!handler) return;

	pollList.push_back(handler->pollInfo(events));
	pollLookup[pollList.size() - 1] = handler;
}

static FDHandler* findHandler(const PollLookup& pollLookup, int pollVector)
{
	PollLookup::const_iterator i = pollLookup.find(pollVector);
	if (i == pollLookup.end())
		return NULL;
	return i->second;
}

int main(int argc, char **argv)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	gMainEnterTime = ts.tv_sec * 1000000000 + ts.tv_nsec;

	po::variables_map vm;
	StrVector args;
	StrVector uncollected;
	std::string script;
	bool spawnAndExit;

	po::options_description desc("Recognized options");
	desc.add_options()
		("help,h", "Print this help message")
		("script,r", po::value<std::string>(), "Script to run")
		("port,p", po::value<int>(&gPort)->default_value(9000), "Port where the service forker is running (default of 9000)")
		("host,H", po::value<std::string>(), "Inet4 address where the service forker is running (default of localhost)")
		("arg", po::value<StrVector>(), "Argument to pass to the script (multiple can be passed")
		("syslog,s", "Log output to syslog")
		("no-stdin,n", "Don't read from stdin")
		("no-childstdout,o", "Don't pipe child's stdout to the fork server (or spawner)")
		("no-childstderr,e", "Don't pipe child's stderr to the fork server (or spawner)")
		("daemon,D", "Run the application in background - quit as soon as child is launched")
	;

	po::positional_options_description pos_opts;
	pos_opts.add("script", 1);
	pos_opts.add("arg", -1);

	try {
		po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).positional(pos_opts).run();
		uncollected = po::collect_unrecognized(parsed.options, po::exclude_positional);
		po::store(parsed, vm);
	} catch (const std::exception& e) {
		std::cerr << "Invalid command line options: " << e.what() << "\n";
		return 3;
	}

	po::notify(vm);

	if (vm.count("help")) {
		std::cout << desc << std::endl;
		return -1;
	}

	if (vm.count("arg")) {
		args = vm["arg"].as<StrVector>();
	}

	if (!vm.count("script")) {
		std::cerr << "No script to run" << "\n" << desc << std::endl;
		return 1;
	}

	if (vm.count("daemon"))
		gRunAsDaemon = true;

	if (vm.count("syslog"))
		gUseSyslog = true;

	if (vm.count("no-stdin"))
		gNoStdin = true;

        if (vm.count("no-childstdout"))
		gNoChildStdout = true;

	if (vm.count("no-childstderr"))
		gNoChildStderr = true;

	script = vm["script"].as<std::string>();

	if (args.empty())
		args = uncollected;
	else
		args.insert(args.end(), uncollected.begin(), uncollected.end());

	int connectedSocket = launch(gPort, gHost, script, args);
	if (connectedSocket == -1) {
		std::cerr << ErrPrefix(false) << "Failed to launch script '" << script << "' on " << gHost << ":" << gPort << std::endl;
		return 2;
	}

	if (!gNoStdin) {
		gStdinHandler = new FDHandler(STDIN_FILENO);
	}
	gChildSocketHandler = new SpawnSocketHandler(script, args, connectedSocket);

	PollVector pollInfo;
	PollLookup pollLookup;

	if (!gNoStdin) {
		addFDListener(pollInfo, pollLookup, gStdinHandler, POLLIN | POLLERR);
	}
	addFDListener(pollInfo, pollLookup, gChildSocketHandler, POLLIN | POLLOUT | POLLHUP | POLLRDHUP | POLLERR);

	int numPollable;
	bool blocked = true;
#if 0
	sigset_t signalsToMask, orig_sigmask;
	sigemptyset(&signalsToMask);
	sigaddset(&signalsToMask, SIGINT);
	sigaddset(&signalsToMask, SIGTERM);
	sigaddset(&signalsToMask, SIGALRM);
	sigaddset(&signalsToMask, SIGUSR1);
	sigaddset(&signalsToMask, SIGUSR2);
#endif

	signal(SIGINT, sh_int);
	signal(SIGTERM, sh_term);
	signal(SIGUSR1, sh_usr);
	signal(SIGUSR2, sh_usr);
	signal(SIGALRM, sh_alarm);

	while (!gFinished) {
		if (sigcnt_TERM || sigcnt_INT) {
			if (sigcnt_INT)
				std::cerr << "\nCtrl-C signal - shutting down\n";
			else
				std::cerr << "Termination request - shutting down\n";
			sigcnt_INT = 0;
			sigcnt_TERM = 0;

			for (int i = 0; i < pollInfo.size(); i++) {
				close(pollInfo[i].fd);
			}
			gExitCode = -1;
			numPollable = 0;
			sigcnt_TERM = 0;
			break;
		}

#if 0
		numPollable = ppoll(pollInfo.data(), pollInfo.size(), NULL, &signalsToMask);
#else
		numPollable = poll(pollInfo.data(), pollInfo.size(), -1);
#endif

		if (numPollable == -1) {
			if (errno == EINTR) {
				continue;
			}
			std::cerr << "Poll error " << strerror(errno) << std::endl;
			break;
		}

		if (numPollable == 0) {
			std::cerr << "Timeout in poll but no timeout specified" << std::endl;
			continue;
		}

		int numHandled = 0;

		PollVector::iterator i, ni;
		int index = 0;
		for (i = pollInfo.begin(), ni = pollInfo.end(); i != ni; ) {
			if (gFinished) break;

			if (i->revents) {
				FDHandler *handler = findHandler(pollLookup, index);
				numHandled++;

				if (i->revents & POLLIN) {
					handler->read();
				}
				if (i->revents & POLLOUT) {
					if (handler->isConnected())
						handler->flush();
				}
				if (i->revents & POLLERR) {
					handler->error();
				}
				if ((i->revents & POLLHUP) || (i->revents & POLLRDHUP)) {
					handler->closed();
				}

				if (!handler->isConnected()) {
					pollLookup.erase(pollLookup.find(index));
					i = pollInfo.erase(i);
					delete handler;
					continue;
				}

				if (handler->hasPendingWrite())
					i->events |= POLLOUT;
				else
					i->events &= ~POLLOUT;
			}

			i++;
			index++;
		}

#if 0
		if (numHandled < numPollable)
			std::cerr << "handled less than indicated pollable" << std::endl;
		else
			std::cerr << "handled " << numHandled << " pollable descriptors" << std::endl;
#endif

		if (!gFinished && pollInfo.size() <= 1) {
			if (pollInfo.empty() || pollInfo[0].fd == STDIN_FILENO) {
				gExitCode = -2;
				gFinished = true;
			}
		}
	}

	if (numPollable == -1) {
		std::cerr << "Trouble polling inputs: " << strerror(errno) << std::endl;
		return 3;
	}

	return gExitCode;
}

