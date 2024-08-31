/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "system.h"

#include <sys/types.h>
#include <sys/stat.h>

#if defined(WEBSOCKETS)
	#include "engine/shared/websockets.h"
#endif

#if defined(CONF_FAMILY_UNIX)
	#include <sys/time.h>
	#include <unistd.h>

	/* unix net includes */
	#include <sys/socket.h>
	#include <sys/ioctl.h>
	#include <errno.h>
	#include <netdb.h>
	#include <netinet/in.h>
	#include <fcntl.h>
	#include <pthread.h>
	#include <arpa/inet.h>

	#include <dirent.h>

	#if defined(CONF_PLATFORM_MACOSX)
		// some lock and pthread functions are already defined in headers
		// included from Carbon.h
		// this prevents having duplicate definitions of those
		#define _lock_set_user_
		#define _task_user_

		#include <Carbon/Carbon.h>
	#endif

	#if defined(__ANDROID__)
		#include <android/log.h>
	#endif

#elif defined(CONF_FAMILY_WINDOWS)
	#define WIN32_LEAN_AND_MEAN
	#undef _WIN32_WINNT
	#define _WIN32_WINNT 0x0501 /* required for mingw to get getaddrinfo to work */
	#include <windows.h>
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <fcntl.h>
	#include <direct.h>
	#include <errno.h>
	#include <process.h>
	#include <shellapi.h>
	#include <wincrypt.h>
#else
	#error NOT IMPLEMENTED
#endif

#if defined(CONF_PLATFORM_SOLARIS)
	#include <sys/filio.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef FUZZING
static unsigned char gs_NetData[1024];
static int gs_NetPosition = 0;
static int gs_NetSize = 0;
#endif

IOHANDLE io_stdin() { return (IOHANDLE)stdin; }
IOHANDLE io_stdout() { return (IOHANDLE)stdout; }
IOHANDLE io_stderr() { return (IOHANDLE)stderr; }

static int num_loggers = 0;

static NETSTATS network_stats = {0};
static MEMSTATS memory_stats = {0};

static NETSOCKET invalid_socket = {NETTYPE_INVALID, -1, -1};

#define AF_WEBSOCKET_INET (0xee)

void dbg_assert_imp(const char *filename, int line, int test, const char *msg)
{
	if(!test)
	{
		log_msgf("assert", "{}({}): {}", filename, line, msg);
		dbg_break();
	}
}

void dbg_break()
{
	*((volatile unsigned*)0) = 0x0;
}

#if !defined(CONF_PLATFORM_MACOSX)
#define QUEUE_SIZE 16

typedef struct
{
	char q[QUEUE_SIZE][1024*4];
	int begin;
	int end;
	SEMAPHORE mutex;
	SEMAPHORE notempty;
	SEMAPHORE notfull;
} Queue;

static int dbg_msg_threaded = 0;
static Queue log_queue;

int queue_empty(Queue *q)
{
	return q->begin == q->end;
}

int queue_full(Queue *q)
{
	return ((q->end+1) % QUEUE_SIZE) == q->begin;
}
#endif

typedef struct MEMHEADER
{
	const char *filename;
	int line;
	int size;
	struct MEMHEADER *prev;
	struct MEMHEADER *next;
} MEMHEADER;

typedef struct MEMTAIL
{
	int guard;
} MEMTAIL;

static struct MEMHEADER *first = 0;
static const int MEM_GUARD_VAL = 0xbaadc0de;

void *mem_alloc_debug(const char *filename, int line, unsigned size, unsigned alignment)
{
	/* TODO: fix alignment */
	/* TODO: add debugging */
	MEMTAIL *tail;
	MEMHEADER *header = (struct MEMHEADER *)malloc(size+sizeof(MEMHEADER)+sizeof(MEMTAIL));
	dbg_assert(header != 0, "mem_alloc failure");
	if(!header)
		return NULL;
	tail = (struct MEMTAIL *)(((char*)(header+1))+size);
	header->size = size;
	header->filename = filename;
	header->line = line;

	memory_stats.allocated += header->size;
	memory_stats.total_allocations++;
	memory_stats.active_allocations++;

	tail->guard = MEM_GUARD_VAL;

	header->prev = (MEMHEADER *)0;
	header->next = first;
	if(first)
		first->prev = header;
	first = header;

	/*dbg_msg("mem", "++ {}", header+1); */
	return header+1;
}

void mem_free(void *p)
{
	if(p)
	{
		MEMHEADER *header = (MEMHEADER *)p - 1;
		MEMTAIL *tail = (MEMTAIL *)(((char*)(header+1))+header->size);

		if(tail->guard != MEM_GUARD_VAL)
			log_msgf("mem", "!! {}", p);
		/* dbg_msg("mem", "-- {}", p); */
		memory_stats.allocated -= header->size;
		memory_stats.active_allocations--;

		if(header->prev)
			header->prev->next = header->next;
		else
			first = header->next;
		if(header->next)
			header->next->prev = header->prev;

		free(header);
	}
}

void mem_debug_dump(IOHANDLE file)
{
	char buf[1024];
	MEMHEADER *header = first;
	if(!file)
		file = io_open("memory.txt", IOFLAG_WRITE);

	if(file)
	{
		while(header)
		{
			str_format(buf, sizeof(buf), "%s(%d): %d", header->filename, header->line, header->size);
			io_write(file, buf, strlen(buf));
			io_write_newline(file);
			header = header->next;
		}

		io_close(file);
	}
}


void mem_copy(void *dest, const void *source, unsigned size)
{
	memcpy(dest, source, size);
}

void mem_move(void *dest, const void *source, unsigned size)
{
	memmove(dest, source, size);
}

void mem_zero(void *block,unsigned size)
{
	memset(block, 0, size);
}

int mem_check_imp()
{
	MEMHEADER *header = first;
	while(header)
	{
		MEMTAIL *tail = (MEMTAIL *)(((char*)(header+1))+header->size);
		if(tail->guard != MEM_GUARD_VAL)
		{
			log_msgf("mem", "Memory check failed at {}({}): {}", header->filename, header->line, header->size);
			return 0;
		}
		header = header->next;
	}

	return 1;
}

IOHANDLE io_open(const char *filename, int flags)
{
	if(flags == IOFLAG_READ)
		return (IOHANDLE)fopen(filename, "rb");
	if(flags == IOFLAG_WRITE)
		return (IOHANDLE)fopen(filename, "wb");
	return 0x0;
}

unsigned io_read(IOHANDLE io, void *buffer, unsigned size)
{
	return fread(buffer, 1, size, (FILE*)io);
}

unsigned io_skip(IOHANDLE io, int size)
{
	fseek((FILE*)io, size, SEEK_CUR);
	return size;
}

int io_seek(IOHANDLE io, int offset, int origin)
{
	int real_origin;

	switch(origin)
	{
	case IOSEEK_START:
		real_origin = SEEK_SET;
		break;
	case IOSEEK_CUR:
		real_origin = SEEK_CUR;
		break;
	case IOSEEK_END:
		real_origin = SEEK_END;
		break;
	default:
		return -1;
	}

	return fseek((FILE*)io, offset, real_origin);
}

long int io_tell(IOHANDLE io)
{
	return ftell((FILE*)io);
}

long int io_length(IOHANDLE io)
{
	long int length;
	io_seek(io, 0, IOSEEK_END);
	length = io_tell(io);
	io_seek(io, 0, IOSEEK_START);
	return length;
}

unsigned io_write(IOHANDLE io, const void *buffer, unsigned size)
{
	return fwrite(buffer, 1, size, (FILE*)io);
}

unsigned io_write_newline(IOHANDLE io)
{
#if defined(CONF_FAMILY_WINDOWS)
	return fwrite("\r\n", 1, 2, (FILE*)io);
#else
	return fwrite("\n", 1, 1, (FILE*)io);
#endif
}

int io_close(IOHANDLE io)
{
	fclose((FILE*)io);
	return 1;
}

int io_flush(IOHANDLE io)
{
	fflush((FILE*)io);
	return 0;
}

void *thread_init(void (*threadfunc)(void *), void *u)
{
#if defined(CONF_FAMILY_UNIX)
	pthread_t id;
	pthread_create(&id, NULL, (void *(*)(void*))threadfunc, u);
	return (void*)id;
#elif defined(CONF_FAMILY_WINDOWS)
	return CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)threadfunc, u, 0, NULL);
#else
	#error not implemented
#endif
}

void thread_wait(void *thread)
{
#if defined(CONF_FAMILY_UNIX)
	pthread_join((pthread_t)thread, NULL);
#elif defined(CONF_FAMILY_WINDOWS)
	WaitForSingleObject((HANDLE)thread, INFINITE);
#else
	#error not implemented
#endif
}

void thread_destroy(void *thread)
{
#if defined(CONF_FAMILY_UNIX)
	void *r = 0;
	pthread_join((pthread_t)thread, &r);
#else
	/*#error not implemented*/
#endif
}

void thread_yield()
{
#if defined(CONF_FAMILY_UNIX)
	sched_yield();
#elif defined(CONF_FAMILY_WINDOWS)
	Sleep(0);
#else
	#error not implemented
#endif
}

void thread_sleep(int milliseconds)
{
#if defined(CONF_FAMILY_UNIX)
	usleep(milliseconds*1000);
#elif defined(CONF_FAMILY_WINDOWS)
	Sleep(milliseconds);
#else
	#error not implemented
#endif
}

void thread_detach(void *thread)
{
#if defined(CONF_FAMILY_UNIX)
	pthread_detach((pthread_t)(thread));
#elif defined(CONF_FAMILY_WINDOWS)
	CloseHandle(thread);
#else
	#error not implemented
#endif
}




#if defined(CONF_FAMILY_UNIX)
typedef pthread_mutex_t LOCKINTERNAL;
#elif defined(CONF_FAMILY_WINDOWS)
typedef CRITICAL_SECTION LOCKINTERNAL;
#else
	#error not implemented on this platform
#endif

LOCK lock_create()
{
	LOCKINTERNAL *lock = (LOCKINTERNAL*)mem_alloc(sizeof(LOCKINTERNAL), 4);

#if defined(CONF_FAMILY_UNIX)
	pthread_mutex_init(lock, 0x0);
#elif defined(CONF_FAMILY_WINDOWS)
	InitializeCriticalSection((LPCRITICAL_SECTION)lock);
#else
	#error not implemented on this platform
#endif
	return (LOCK)lock;
}

void lock_destroy(LOCK lock)
{
#if defined(CONF_FAMILY_UNIX)
	pthread_mutex_destroy((LOCKINTERNAL *)lock);
#elif defined(CONF_FAMILY_WINDOWS)
	DeleteCriticalSection((LPCRITICAL_SECTION)lock);
#else
	#error not implemented on this platform
#endif
	mem_free(lock);
}

int lock_trylock(LOCK lock)
{
#if defined(CONF_FAMILY_UNIX)
	return pthread_mutex_trylock((LOCKINTERNAL *)lock);
#elif defined(CONF_FAMILY_WINDOWS)
	return !TryEnterCriticalSection((LPCRITICAL_SECTION)lock);
#else
	#error not implemented on this platform
#endif
}

void lock_wait(LOCK lock)
{
#if defined(CONF_FAMILY_UNIX)
	pthread_mutex_lock((LOCKINTERNAL *)lock);
#elif defined(CONF_FAMILY_WINDOWS)
	EnterCriticalSection((LPCRITICAL_SECTION)lock);
#else
	#error not implemented on this platform
#endif
}

void lock_unlock(LOCK lock)
{
#if defined(CONF_FAMILY_UNIX)
	pthread_mutex_unlock((LOCKINTERNAL *)lock);
#elif defined(CONF_FAMILY_WINDOWS)
	LeaveCriticalSection((LPCRITICAL_SECTION)lock);
#else
	#error not implemented on this platform
#endif
}

#if !defined(CONF_PLATFORM_MACOSX)
	#if defined(CONF_FAMILY_UNIX)
	void semaphore_init(SEMAPHORE *sem) { sem_init(sem, 0, 0); }
	void semaphore_wait(SEMAPHORE *sem) { sem_wait(sem); }
	void semaphore_signal(SEMAPHORE *sem) { sem_post(sem); }
	void semaphore_destroy(SEMAPHORE *sem) { sem_destroy(sem); }
	#elif defined(CONF_FAMILY_WINDOWS)
	void semaphore_init(SEMAPHORE *sem) { *sem = CreateSemaphore(0, 0, 10000, 0); }
	void semaphore_wait(SEMAPHORE *sem) { WaitForSingleObject((HANDLE)*sem, INFINITE); }
	void semaphore_signal(SEMAPHORE *sem) { ReleaseSemaphore((HANDLE)*sem, 1, NULL); }
	void semaphore_destroy(SEMAPHORE *sem) { CloseHandle((HANDLE)*sem); }
	#else
		#error not implemented on this platform
	#endif
#endif

static int new_tick = -1;

void set_new_tick()
{
	new_tick = 1;
}

/* -----  time ----- */
int64 time_get()
{
	static int64 last = 0;
	if(!new_tick)
		return last;
	if(new_tick != -1)
		new_tick = 0;

#if defined(CONF_FAMILY_UNIX)
	struct timeval val;
	gettimeofday(&val, NULL);
	last = (int64)val.tv_sec*(int64)1000000+(int64)val.tv_usec;
	return last;
#elif defined(CONF_FAMILY_WINDOWS)
	{
		int64 t;
		QueryPerformanceCounter((PLARGE_INTEGER)&t);
		if(t<last) /* for some reason, QPC can return values in the past */
			return last;
		last = t;
		return t;
	}
#else
	#error not implemented
#endif
}

int64 time_freq()
{
#if defined(CONF_FAMILY_UNIX)
	return 1000000;
#elif defined(CONF_FAMILY_WINDOWS)
	int64 t;
	QueryPerformanceFrequency((PLARGE_INTEGER)&t);
	return t;
#else
	#error not implemented
#endif
}

/* -----  network ----- */
static void netaddr_to_sockaddr_in(const NETADDR *src, struct sockaddr_in *dest)
{
	mem_zero(dest, sizeof(struct sockaddr_in));
	if(src->type != NETTYPE_IPV4 && src->type != NETTYPE_WEBSOCKET_IPV4)
	{
		log_msgf("system", "couldn't convert NETADDR of type {} to ipv4", src->type);
		return;
	}

	dest->sin_family = AF_INET;
	dest->sin_port = htons(src->port);
	mem_copy(&dest->sin_addr.s_addr, src->ip, 4);
}

static void netaddr_to_sockaddr_in6(const NETADDR *src, struct sockaddr_in6 *dest)
{
	mem_zero(dest, sizeof(struct sockaddr_in6));
	if(src->type != NETTYPE_IPV6)
	{
		log_msgf("system", "couldn't not convert NETADDR of type {} to ipv6", src->type);
		return;
	}

	dest->sin6_family = AF_INET6;
	dest->sin6_port = htons(src->port);
	mem_copy(&dest->sin6_addr.s6_addr, src->ip, 16);
}

static void sockaddr_to_netaddr(const struct sockaddr *src, NETADDR *dst)
{
	if(src->sa_family == AF_INET)
	{
		mem_zero(dst, sizeof(NETADDR));
		dst->type = NETTYPE_IPV4;
		dst->port = htons(((struct sockaddr_in*)src)->sin_port);
		mem_copy(dst->ip, &((struct sockaddr_in*)src)->sin_addr.s_addr, 4);
	}
	else if(src->sa_family == AF_WEBSOCKET_INET)
	{
		mem_zero(dst, sizeof(NETADDR));
		dst->type = NETTYPE_WEBSOCKET_IPV4;
		dst->port = htons(((struct sockaddr_in*)src)->sin_port);
		mem_copy(dst->ip, &((struct sockaddr_in*)src)->sin_addr.s_addr, 4);
	}
	else if(src->sa_family == AF_INET6)
	{
		mem_zero(dst, sizeof(NETADDR));
		dst->type = NETTYPE_IPV6;
		dst->port = htons(((struct sockaddr_in6*)src)->sin6_port);
		mem_copy(dst->ip, &((struct sockaddr_in6*)src)->sin6_addr.s6_addr, 16);
	}
	else
	{
		mem_zero(dst, sizeof(struct sockaddr));
		log_msgf("system", "couldn't convert sockaddr of family {}", src->sa_family);
	}
}

int net_addr_comp(const NETADDR *a, const NETADDR *b)
{
	return mem_comp(a, b, sizeof(NETADDR));
}

void net_addr_str(const NETADDR *addr, char *string, int max_length, int add_port)
{
	if(addr->type == NETTYPE_IPV4 || addr->type == NETTYPE_WEBSOCKET_IPV4)
	{
		if(add_port != 0)
			str_format(string, max_length, "%d.%d.%d.%d:%d", addr->ip[0], addr->ip[1], addr->ip[2], addr->ip[3], addr->port);
		else
			str_format(string, max_length, "%d.%d.%d.%d", addr->ip[0], addr->ip[1], addr->ip[2], addr->ip[3]);
	}
	else if(addr->type == NETTYPE_IPV6)
	{
		if(add_port != 0)
			str_format(string, max_length, "[%x:%x:%x:%x:%x:%x:%x:%x]:{}",
				(addr->ip[0]<<8)|addr->ip[1], (addr->ip[2]<<8)|addr->ip[3], (addr->ip[4]<<8)|addr->ip[5], (addr->ip[6]<<8)|addr->ip[7],
				(addr->ip[8]<<8)|addr->ip[9], (addr->ip[10]<<8)|addr->ip[11], (addr->ip[12]<<8)|addr->ip[13], (addr->ip[14]<<8)|addr->ip[15],
				addr->port);
		else
			str_format(string, max_length, "[%x:%x:%x:%x:%x:%x:%x:%x]",
				(addr->ip[0]<<8)|addr->ip[1], (addr->ip[2]<<8)|addr->ip[3], (addr->ip[4]<<8)|addr->ip[5], (addr->ip[6]<<8)|addr->ip[7],
				(addr->ip[8]<<8)|addr->ip[9], (addr->ip[10]<<8)|addr->ip[11], (addr->ip[12]<<8)|addr->ip[13], (addr->ip[14]<<8)|addr->ip[15]);
	}
	else
		str_format(string, max_length, "unknown type %d", addr->type);
}

static int priv_net_extract(const char *hostname, char *host, int max_host, int *port)
{
	int i;

	*port = 0;
	host[0] = 0;

	if(hostname[0] == '[')
	{
		// ipv6 mode
		for(i = 1; i < max_host && hostname[i] && hostname[i] != ']'; i++)
			host[i-1] = hostname[i];
		host[i-1] = 0;
		if(hostname[i] != ']') // malformatted
			return -1;

		i++;
		if(hostname[i] == ':')
			*port = atol(hostname+i+1);
	}
	else
	{
		// generic mode (ipv4, hostname etc)
		for(i = 0; i < max_host-1 && hostname[i] && hostname[i] != ':'; i++)
			host[i] = hostname[i];
		host[i] = 0;

		if(hostname[i] == ':')
			*port = atol(hostname+i+1);
	}

	return 0;
}

int net_host_lookup(const char *hostname, NETADDR *addr, int types)
{
	struct addrinfo hints;
	struct addrinfo *result = NULL;
	int e;
	char host[256];
	int port = 0;

	if(priv_net_extract(hostname, host, sizeof(host), &port))
		return -1;

	log_msgf("host lookup", "host='{}' port={} {}", host, port, types);

	mem_zero(&hints, sizeof(hints));

	hints.ai_family = AF_UNSPEC;

	if(types == NETTYPE_IPV4)
		hints.ai_family = AF_INET;
	else if(types == NETTYPE_IPV6)
		hints.ai_family = AF_INET6;

	e = getaddrinfo(host, NULL, &hints, &result);

	if(!result)
		return -1;

	if(e != 0)
	{
		freeaddrinfo(result);
		return -1;
	}

	sockaddr_to_netaddr(result->ai_addr, addr);
	addr->port = port;
	freeaddrinfo(result);
	return 0;
}

static int parse_int(int *out, const char **str)
{
	int i = 0;
	*out = 0;
	if(**str < '0' || **str > '9')
		return -1;

	i = **str - '0';
	(*str)++;

	while(1)
	{
		if(**str < '0' || **str > '9')
		{
			*out = i;
			return 0;
		}

		i = (i*10) + (**str - '0');
		(*str)++;
	}

	return 0;
}

static int parse_char(char c, const char **str)
{
	if(**str != c) return -1;
	(*str)++;
	return 0;
}

static int parse_uint8(unsigned char *out, const char **str)
{
	int i;
	if(parse_int(&i, str) != 0) return -1;
	if(i < 0 || i > 0xff) return -1;
	*out = i;
	return 0;
}

static int parse_uint16(unsigned short *out, const char **str)
{
	int i;
	if(parse_int(&i, str) != 0) return -1;
	if(i < 0 || i > 0xffff) return -1;
	*out = i;
	return 0;
}

int net_addr_from_str(NETADDR *addr, const char *string)
{
	const char *str = string;
	mem_zero(addr, sizeof(NETADDR));

	if(str[0] == '[')
	{
		/* ipv6 */
		struct sockaddr_in6 sa6;
		char buf[128];
		int i;
		str++;
		for(i = 0; i < 127 && str[i] && str[i] != ']'; i++)
			buf[i] = str[i];
		buf[i] = 0;
		str += i;
#if defined(CONF_FAMILY_WINDOWS)
		{
			int size;
			sa6.sin6_family = AF_INET6;
			size = (int)sizeof(sa6);
			if(WSAStringToAddress(buf, AF_INET6, NULL, (struct sockaddr *)&sa6, &size) != 0)
				return -1;
		}
#else
		sa6.sin6_family = AF_INET6;

		if(inet_pton(AF_INET6, buf, &sa6.sin6_addr) != 1)
			return -1;
#endif
		sockaddr_to_netaddr((struct sockaddr *)&sa6, addr);

		if(*str == ']')
		{
			str++;
			if(*str == ':')
			{
				str++;
				if(parse_uint16(&addr->port, &str))
					return -1;
			}
		}
		else
			return -1;

		return 0;
	}
	else
	{
		/* ipv4 */
		if(parse_uint8(&addr->ip[0], &str)) return -1;
		if(parse_char('.', &str)) return -1;
		if(parse_uint8(&addr->ip[1], &str)) return -1;
		if(parse_char('.', &str)) return -1;
		if(parse_uint8(&addr->ip[2], &str)) return -1;
		if(parse_char('.', &str)) return -1;
		if(parse_uint8(&addr->ip[3], &str)) return -1;
		if(*str == ':')
		{
			str++;
			if(parse_uint16(&addr->port, &str)) return -1;
		}

		addr->type = NETTYPE_IPV4;
	}

	return 0;
}

static void priv_net_close_socket(int sock)
{
#if defined(CONF_FAMILY_WINDOWS)
	closesocket(sock);
#else
	close(sock);
#endif
}

static int priv_net_close_all_sockets(NETSOCKET sock)
{
	/* close down ipv4 */
	if(sock.ipv4sock >= 0)
	{
		priv_net_close_socket(sock.ipv4sock);
		sock.ipv4sock = -1;
		sock.type &= ~NETTYPE_IPV4;
	}

	/* close down ipv6 */
	if(sock.ipv6sock >= 0)
	{
		priv_net_close_socket(sock.ipv6sock);
		sock.ipv6sock = -1;
		sock.type &= ~NETTYPE_IPV6;
	}
	return 0;
}

static int priv_net_create_socket(int domain, int type, struct sockaddr *addr, int sockaddrlen, int use_random_port)
{
	int sock, e;

	/* create socket */
	sock = socket(domain, type, 0);
	if(sock < 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		char buf[128];
		int error = WSAGetLastError();
		if(FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS, 0, error, 0, buf, sizeof(buf), 0) == 0)
			buf[0] = 0;
		log_msgf("net", "failed to create socket with domain {} and type {} ({} '{}')", domain, type, error, buf);
#else
		log_msgf("net", "failed to create socket with domain {} and type {} ({} '{}')", domain, type, errno, strerror(errno));
#endif
		return -1;
	}

	/* set to IPv6 only if thats what we are creating */
#if defined(IPV6_V6ONLY)	/* windows sdk 6.1 and higher */
	if(domain == AF_INET6)
	{
		int ipv6only = 1;
		setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&ipv6only, sizeof(ipv6only));
	}
#endif

	/* bind the socket */
	while(1)
	{
		/* pick random port */
		if(use_random_port)
		{
			int port = htons(rand()%16384+49152);	/* 49152 to 65535 */
			if(domain == AF_INET)
				((struct sockaddr_in *)(addr))->sin_port = port;
			else
				((struct sockaddr_in6 *)(addr))->sin6_port = port;
		}

		e = bind(sock, addr, sockaddrlen);
		if(e == 0)
			break;
		else
		{
#if defined(CONF_FAMILY_WINDOWS)
			char buf[128];
			int error = WSAGetLastError();
			if(error == WSAEADDRINUSE && use_random_port)
				continue;
			if(FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS, 0, error, 0, buf, sizeof(buf), 0) == 0)
				buf[0] = 0;
			log_msgf("net", "failed to bind socket with domain {} and type {} ({} '{}')", domain, type, error, buf);
#else
			if(errno == EADDRINUSE && use_random_port)
				continue;
			log_msgf("net", "failed to bind socket with domain {} and type {} ({} '{}')", domain, type, errno, strerror(errno));
#endif
			priv_net_close_socket(sock);
			return -1;
		}
	}

	/* return the newly created socket */
	return sock;
}

NETSOCKET net_udp_create(NETADDR bindaddr)
{
	NETSOCKET sock = invalid_socket;
	NETADDR tmpbindaddr = bindaddr;
	int broadcast = 1;
	int recvsize = 65536;

	if(bindaddr.type&NETTYPE_IPV4)
	{
		struct sockaddr_in addr;
		int socket = -1;

		/* bind, we should check for error */
		tmpbindaddr.type = NETTYPE_IPV4;
		netaddr_to_sockaddr_in(&tmpbindaddr, &addr);
		socket = priv_net_create_socket(AF_INET, SOCK_DGRAM, (struct sockaddr *)&addr, sizeof(addr), 1);
		if(socket >= 0)
		{
			sock.type |= NETTYPE_IPV4;
			sock.ipv4sock = socket;

			/* set boardcast */
			setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast));

			/* set receive buffer size */
			setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*)&recvsize, sizeof(recvsize));
		}
	}

	if(bindaddr.type&NETTYPE_IPV6)
	{
		struct sockaddr_in6 addr;
		int socket = -1;

		/* bind, we should check for error */
		tmpbindaddr.type = NETTYPE_IPV6;
		netaddr_to_sockaddr_in6(&tmpbindaddr, &addr);
		socket = priv_net_create_socket(AF_INET6, SOCK_DGRAM, (struct sockaddr *)&addr, sizeof(addr), 1);
		if(socket >= 0)
		{
			sock.type |= NETTYPE_IPV6;
			sock.ipv6sock = socket;

			/* set boardcast */
			setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast));

			/* set receive buffer size */
			setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*)&recvsize, sizeof(recvsize));
		}
	}

	/* set non-blocking */
	net_set_non_blocking(sock);

	/* return */
	return sock;
}

int net_udp_send(NETSOCKET sock, const NETADDR *addr, const void *data, int size)
{
	int d = -1;

	if(addr->type&NETTYPE_IPV4)
	{
		if(sock.ipv4sock >= 0)
		{
			struct sockaddr_in sa;
			if(addr->type&NETTYPE_LINK_BROADCAST)
			{
				mem_zero(&sa, sizeof(sa));
				sa.sin_port = htons(addr->port);
				sa.sin_family = AF_INET;
				sa.sin_addr.s_addr = INADDR_BROADCAST;
			}
			else
				netaddr_to_sockaddr_in(addr, &sa);

			d = sendto((int)sock.ipv4sock, (const char*)data, size, 0, (struct sockaddr *)&sa, sizeof(sa));
		}
		else
			log_msg("net", "can't sent ipv4 traffic to this socket");
	}

	if(addr->type&NETTYPE_IPV6)
	{
		if(sock.ipv6sock >= 0)
		{
			struct sockaddr_in6 sa;
			if(addr->type&NETTYPE_LINK_BROADCAST)
			{
				mem_zero(&sa, sizeof(sa));
				sa.sin6_port = htons(addr->port);
				sa.sin6_family = AF_INET6;
				sa.sin6_addr.s6_addr[0] = 0xff; /* multicast */
				sa.sin6_addr.s6_addr[1] = 0x02; /* link local scope */
				sa.sin6_addr.s6_addr[15] = 1; /* all nodes */
			}
			else
				netaddr_to_sockaddr_in6(addr, &sa);

			d = sendto((int)sock.ipv6sock, (const char*)data, size, 0, (struct sockaddr *)&sa, sizeof(sa));
		}
		else
			log_msg("net", "can't sent ipv6 traffic to this socket");
	}
	/*
	else
		dbg_msg("net", "can't sent to network of type %d", addr->type);
		*/

	/*if(d < 0)
	{
		char addrstr[256];
		net_addr_str(addr, addrstr, sizeof(addrstr));

		dbg_msg("net", "sendto error (%d '%s')", errno, strerror(errno));
		dbg_msg("net", "\tsock = %d %x", sock, sock);
		dbg_msg("net", "\tsize = %d %x", size, size);
		dbg_msg("net", "\taddr = %s", addrstr);

	}*/
	network_stats.sent_bytes += size;
	network_stats.sent_packets++;
	return d;
}

int net_udp_recv(NETSOCKET sock, NETADDR *addr, void *data, int maxsize)
{
	char sockaddrbuf[128];
	socklen_t fromlen;// = sizeof(sockaddrbuf);
	int bytes = 0;

	if(bytes == 0 && sock.ipv4sock >= 0)
	{
		fromlen = sizeof(struct sockaddr_in);
		bytes = recvfrom(sock.ipv4sock, (char*)data, maxsize, 0, (struct sockaddr *)&sockaddrbuf, &fromlen);
	}

	if(bytes <= 0 && sock.ipv6sock >= 0)
	{
		fromlen = sizeof(struct sockaddr_in6);
		bytes = recvfrom(sock.ipv6sock, (char*)data, maxsize, 0, (struct sockaddr *)&sockaddrbuf, &fromlen);
	}

	if(bytes > 0)
	{
		sockaddr_to_netaddr((struct sockaddr *)&sockaddrbuf, addr);
		network_stats.recv_bytes += bytes;
		network_stats.recv_packets++;
		return bytes;
	}
	else if(bytes == 0)
		return 0;
	return -1; /* error */
}

int net_udp_close(NETSOCKET sock)
{
	return priv_net_close_all_sockets(sock);
}

NETSOCKET net_tcp_create(NETADDR bindaddr)
{
	NETSOCKET sock = invalid_socket;
	NETADDR tmpbindaddr = bindaddr;

	if(bindaddr.type&NETTYPE_IPV4)
	{
		struct sockaddr_in addr;
		int socket = -1;

		/* bind, we should check for error */
		tmpbindaddr.type = NETTYPE_IPV4;
		netaddr_to_sockaddr_in(&tmpbindaddr, &addr);
		socket = priv_net_create_socket(AF_INET, SOCK_STREAM, (struct sockaddr *)&addr, sizeof(addr), 0);
		if(socket >= 0)
		{
			sock.type |= NETTYPE_IPV4;
			sock.ipv4sock = socket;
		}
	}

	if(bindaddr.type&NETTYPE_IPV6)
	{
		struct sockaddr_in6 addr;
		int socket = -1;

		/* bind, we should check for error */
		tmpbindaddr.type = NETTYPE_IPV6;
		netaddr_to_sockaddr_in6(&tmpbindaddr, &addr);
		socket = priv_net_create_socket(AF_INET6, SOCK_STREAM, (struct sockaddr *)&addr, sizeof(addr), 0);
		if(socket >= 0)
		{
			sock.type |= NETTYPE_IPV6;
			sock.ipv6sock = socket;
		}
	}

	/* return */
	return sock;
}

int net_set_non_blocking(NETSOCKET sock)
{
	unsigned long mode = 1;
	if(sock.ipv4sock >= 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		ioctlsocket(sock.ipv4sock, FIONBIO, (unsigned long *)&mode);
#else
		ioctl(sock.ipv4sock, FIONBIO, (unsigned long *)&mode);
#endif
	}

	if(sock.ipv6sock >= 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		ioctlsocket(sock.ipv6sock, FIONBIO, (unsigned long *)&mode);
#else
		ioctl(sock.ipv6sock, FIONBIO, (unsigned long *)&mode);
#endif
	}

	return 0;
}

int net_set_blocking(NETSOCKET sock)
{
	unsigned long mode = 0;
	if(sock.ipv4sock >= 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		ioctlsocket(sock.ipv4sock, FIONBIO, (unsigned long *)&mode);
#else
		ioctl(sock.ipv4sock, FIONBIO, (unsigned long *)&mode);
#endif
	}

	if(sock.ipv6sock >= 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		ioctlsocket(sock.ipv6sock, FIONBIO, (unsigned long *)&mode);
#else
		ioctl(sock.ipv6sock, FIONBIO, (unsigned long *)&mode);
#endif
	}

	return 0;
}

int net_tcp_listen(NETSOCKET sock, int backlog)
{
	int err = -1;
	if(sock.ipv4sock >= 0)
		err = listen(sock.ipv4sock, backlog);
	if(sock.ipv6sock >= 0)
		err = listen(sock.ipv6sock, backlog);
	return err;
}

int net_tcp_accept(NETSOCKET sock, NETSOCKET *new_sock, NETADDR *a)
{
	int s;
	socklen_t sockaddr_len;

	*new_sock = invalid_socket;

	if(sock.ipv4sock >= 0)
	{
		struct sockaddr_in addr;
		sockaddr_len = sizeof(addr);

		s = accept(sock.ipv4sock, (struct sockaddr *)&addr, &sockaddr_len);

		if (s != -1)
		{
			sockaddr_to_netaddr((const struct sockaddr *)&addr, a);
			new_sock->type = NETTYPE_IPV4;
			new_sock->ipv4sock = s;
			return s;
		}
	}

	if(sock.ipv6sock >= 0)
	{
		struct sockaddr_in6 addr;
		sockaddr_len = sizeof(addr);

		s = accept(sock.ipv6sock, (struct sockaddr *)&addr, &sockaddr_len);

		if (s != -1)
		{
			sockaddr_to_netaddr((const struct sockaddr *)&addr, a);
			new_sock->type = NETTYPE_IPV6;
			new_sock->ipv6sock = s;
			return s;
		}
	}

	return -1;
}

int net_tcp_connect(NETSOCKET sock, const NETADDR *a)
{
	if(a->type&NETTYPE_IPV4)
	{
		struct sockaddr_in addr;
		netaddr_to_sockaddr_in(a, &addr);
		return connect(sock.ipv4sock, (struct sockaddr *)&addr, sizeof(addr));
	}

	if(a->type&NETTYPE_IPV6)
	{
		struct sockaddr_in6 addr;
		netaddr_to_sockaddr_in6(a, &addr);
		return connect(sock.ipv6sock, (struct sockaddr *)&addr, sizeof(addr));
	}

	return -1;
}

int net_tcp_connect_non_blocking(NETSOCKET sock, NETADDR bindaddr)
{
	int res = 0;

	net_set_non_blocking(sock);
	res = net_tcp_connect(sock, &bindaddr);
	net_set_blocking(sock);

	return res;
}

int net_tcp_send(NETSOCKET sock, const void *data, int size)
{
	int bytes = -1;

	if(sock.ipv4sock >= 0)
		bytes = send((int)sock.ipv4sock, (const char*)data, size, 0);
	if(sock.ipv6sock >= 0)
		bytes = send((int)sock.ipv6sock, (const char*)data, size, 0);

	return bytes;
}

int net_tcp_recv(NETSOCKET sock, void *data, int maxsize)
{
	int bytes = -1;

	if(sock.ipv4sock >= 0)
		bytes = recv((int)sock.ipv4sock, (char*)data, maxsize, 0);
	if(sock.ipv6sock >= 0)
		bytes = recv((int)sock.ipv6sock, (char*)data, maxsize, 0);

	return bytes;
}

int net_tcp_close(NETSOCKET sock)
{
	return priv_net_close_all_sockets(sock);
}

int net_errno()
{
#if defined(CONF_FAMILY_WINDOWS)
	return WSAGetLastError();
#else
	return errno;
#endif
}

int net_would_block()
{
#if defined(CONF_FAMILY_WINDOWS)
	return net_errno() == WSAEWOULDBLOCK;
#else
	return net_errno() == EWOULDBLOCK;
#endif
}

int net_init()
{
#if defined(CONF_FAMILY_WINDOWS)
	WSADATA wsaData;
	int err = WSAStartup(MAKEWORD(1, 1), &wsaData);
	dbg_assert(err == 0, "network initialization failed.");
	return err==0?0:1;
#endif

	return 0;
}

int fs_listdir_info(const char *dir, FS_LISTDIR_INFO_CALLBACK cb, int type, void *user)
{
#if defined(CONF_FAMILY_WINDOWS)
	WIN32_FIND_DATA finddata;
	HANDLE handle;
	char buffer[1024*2];
	int length;
	str_format(buffer, sizeof(buffer), "%s/*", dir);

	handle = FindFirstFileA(buffer, &finddata);

	if (handle == INVALID_HANDLE_VALUE)
		return 0;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	/* add all the entries */
	do
	{
		str_copy(buffer+length, finddata.cFileName, (int)sizeof(buffer)-length);
		if(cb(finddata.cFileName, fs_getmtime(buffer), fs_is_dir(buffer), type, user))
			break;
	}
	while (FindNextFileA(handle, &finddata));

	FindClose(handle);
	return 0;
#else
	struct dirent *entry;
	char buffer[1024*2];
	int length;
	DIR *d = opendir(dir);

	if(!d)
		return 0;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	while((entry = readdir(d)) != NULL)
	{
		str_copy(buffer+length, entry->d_name, (int)sizeof(buffer)-length);
		if(cb(entry->d_name, fs_getmtime(buffer), fs_is_dir(buffer), type, user))
			break;
	}

	/* close the directory and return */
	closedir(d);
	return 0;
#endif
}

int fs_listdir(const char *dir, FS_LISTDIR_CALLBACK cb, int type, void *user)
{
#if defined(CONF_FAMILY_WINDOWS)
	WIN32_FIND_DATA finddata;
	HANDLE handle;
	char buffer[1024*2];
	int length;
	str_format(buffer, sizeof(buffer), "%s/*", dir);

	handle = FindFirstFileA(buffer, &finddata);

	if (handle == INVALID_HANDLE_VALUE)
		return 0;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	/* add all the entries */
	do
	{
		str_copy(buffer+length, finddata.cFileName, (int)sizeof(buffer)-length);
		if(cb(finddata.cFileName, fs_is_dir(buffer), type, user))
			break;
	}
	while (FindNextFileA(handle, &finddata));

	FindClose(handle);
	return 0;
#else
	struct dirent *entry;
	char buffer[1024*2];
	int length;
	DIR *d = opendir(dir);

	if(!d)
		return 0;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	while((entry = readdir(d)) != NULL)
	{
		str_copy(buffer+length, entry->d_name, (int)sizeof(buffer)-length);
		if(cb(entry->d_name, fs_is_dir(buffer), type, user))
			break;
	}

	/* close the directory and return */
	closedir(d);
	return 0;
#endif
}

int fs_storage_path(const char *appname, char *path, int max)
{
#if defined(CONF_FAMILY_WINDOWS)
	char *home = getenv("APPDATA");
	if(!home)
		return -1;
	_snprintf(path, max, "{}/{}", home, appname);
	return 0;
#else
	char *home = getenv("HOME");
#if !defined(CONF_PLATFORM_MACOSX)
	int i;
#endif
	if(!home)
		return -1;

#if defined(CONF_PLATFORM_MACOSX)
	snprintf(path, max, "{}/Library/Application Support/{}", home, appname);
#else
	snprintf(path, max, "{}/.{}", home, appname);
	for(i = strlen(home)+2; path[i]; i++)
		path[i] = tolower(path[i]);
#endif

	return 0;
#endif
}

int fs_makedir(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(_mkdir(path) == 0)
			return 0;
	if(errno == EEXIST)
		return 0;
	return -1;
#else
	if(mkdir(path, 0755) == 0)
		return 0;
	if(errno == EEXIST)
		return 0;
	return -1;
#endif
}

int fs_is_dir(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	/* TODO: do this smarter */
	WIN32_FIND_DATA finddata;
	HANDLE handle;
	char buffer[1024*2];
	str_format(buffer, sizeof(buffer), "%s/*", path);

	if ((handle = FindFirstFileA(buffer, &finddata)) == INVALID_HANDLE_VALUE)
		return 0;

	FindClose(handle);
	return 1;
#else
	struct stat sb;
	if (stat(path, &sb) == -1)
		return 0;

	if (S_ISDIR(sb.st_mode))
		return 1;
	else
		return 0;
#endif
}

time_t fs_getmtime(const char *path)
{
	struct stat sb;
	if (stat(path, &sb) == -1)
		return 0;

	return sb.st_mtime;
}

int fs_chdir(const char *path)
{
	if(fs_is_dir(path))
	{
		if(chdir(path))
			return 1;
		else
			return 0;
	}
	else
		return 1;
}

char *fs_getcwd(char *buffer, int buffer_size)
{
	if(buffer == 0)
		return 0;
#if defined(CONF_FAMILY_WINDOWS)
	return _getcwd(buffer, buffer_size);
#else
	return getcwd(buffer, buffer_size);
#endif
}

int fs_parent_dir(char *path)
{
	char *parent = 0;
	for(; *path; ++path)
	{
		if(*path == '/' || *path == '\\')
			parent = path;
	}

	if(parent)
	{
		*parent = 0;
		return 0;
	}
	return 1;
}

int fs_remove(const char *filename)
{
	if(remove(filename) != 0)
		return 1;
	return 0;
}

int fs_rename(const char *oldname, const char *newname)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(MoveFileEx(oldname, newname, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0)
		return 1;
#else
	if(rename(oldname, newname) != 0)
		return 1;
#endif
	return 0;
}

void swap_endian(void *data, unsigned elem_size, unsigned num)
{
	char *src = (char*) data;
	char *dst = src + (elem_size - 1);

	while(num)
	{
		unsigned n = elem_size>>1;
		char tmp;
		while(n)
		{
			tmp = *src;
			*src = *dst;
			*dst = tmp;

			src++;
			dst--;
			n--;
		}

		src = src + (elem_size>>1);
		dst = src + (elem_size - 1);
		num--;
	}
}

int net_socket_read_wait(NETSOCKET sock, int time)
{
	struct timeval tv;
	fd_set readfds;
	int sockid;

	tv.tv_sec = time / 1000000;
	tv.tv_usec = time % 1000000;
	sockid = 0;

	FD_ZERO(&readfds);
	if(sock.ipv4sock >= 0)
	{
		FD_SET(sock.ipv4sock, &readfds);
		sockid = sock.ipv4sock;
	}
	if(sock.ipv6sock >= 0)
	{
		FD_SET(sock.ipv6sock, &readfds);
		if(sock.ipv6sock > sockid)
			sockid = sock.ipv6sock;
	}
#if defined(WEBSOCKETS)
	if(sock.web_ipv4sock >= 0)
	{
		int maxfd = websocket_fd_set(sock.web_ipv4sock, &readfds);
		if (maxfd > sockid)
			sockid = maxfd;
	}
#endif

	/* don't care about writefds and exceptfds */
	if(time < 0)
		select(sockid+1, &readfds, NULL, NULL, NULL);
	else
		select(sockid+1, &readfds, NULL, NULL, &tv);

	if(sock.ipv4sock >= 0 && FD_ISSET(sock.ipv4sock, &readfds))
		return 1;

	if(sock.ipv6sock >= 0 && FD_ISSET(sock.ipv6sock, &readfds))
		return 1;

	return 0;
}

int time_timestamp()
{
	return time(0);
}

void str_append(char *dst, const char *src, int dst_size)
{
	int s = strlen(dst);
	int i = 0;
	while(s < dst_size)
	{
		dst[s] = src[i];
		if(!src[i]) /* check for null termination */
			break;
		s++;
		i++;
	}

	dst[dst_size-1] = 0; /* assure null termination */
}

void str_copy(char *dst, const char *src, int dst_size)
{
	strncpy(dst, src, dst_size);
	dst[dst_size-1] = 0; /* assure null termination */
}

int str_length(const char *str)
{
	return (int)strlen(str);
}

int str_format(char *buffer, int buffer_size, const char *format, ...)
{
	int ret;
#if defined(CONF_FAMILY_WINDOWS)
	va_list ap;
	va_start(ap, format);
	ret = _vsnprintf(buffer, buffer_size, format, ap);
	va_end(ap);
#else
	va_list ap;
	va_start(ap, format);
	ret = vsnprintf(buffer, buffer_size, format, ap);
	va_end(ap);
#endif

	buffer[buffer_size-1] = 0; /* assure null termination */
	return ret;
}

char *str_trim_words(char *str, int words)
{
	while (words && *str)
	{
		if (isspace(*str) && !isspace(*(str + 1)))
			words--;
		str++;
	}
	return str;
}

/* makes sure that the string only contains the characters between 32 and 127 */
void str_sanitize_strong(char *str_in)
{
	unsigned char *str = (unsigned char *)str_in;
	while(*str)
	{
		*str &= 0x7f;
		if(*str < 32)
			*str = 32;
		str++;
	}
}

/* makes sure that the string only contains the characters between 32 and 255 */
void str_sanitize_cc(char *str_in)
{
	unsigned char *str = (unsigned char *)str_in;
	while(*str)
	{
		if(*str < 32)
			*str = ' ';
		str++;
	}
}

/* makes sure that the string only contains the characters between 32 and 255 + \r\n\t */
void str_sanitize(char *str_in)
{
	unsigned char *str = (unsigned char *)str_in;
	while(*str)
	{
		if(*str < 32 && !(*str == '\r') && !(*str == '\n') && !(*str == '\t'))
			*str = ' ';
		str++;
	}
}

char *str_skip_to_whitespace(char *str)
{
	while(*str && (*str != ' ' && *str != '\t' && *str != '\n'))
		str++;
	return str;
}

char *str_skip_whitespaces(char *str)
{
	while(*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r'))
		str++;
	return str;
}

/* case */
int str_comp_nocase(const char *a, const char *b)
{
#if defined(CONF_FAMILY_WINDOWS)
	return _stricmp(a,b);
#else
	return strcasecmp(a,b);
#endif
}

int str_comp_nocase_num(const char *a, const char *b, const int num)
{
#if defined(CONF_FAMILY_WINDOWS)
	return _strnicmp(a, b, num);
#else
	return strncasecmp(a, b, num);
#endif
}

int str_comp(const char *a, const char *b)
{
	return strcmp(a, b);
}

int str_comp_num(const char *a, const char *b, const int num)
{
	return strncmp(a, b, num);
}

int str_comp_filenames(const char *a, const char *b)
{
	int result;

	for(; *a && *b; ++a, ++b)
	{
		if(*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9')
		{
			result = 0;
			do
			{
				if(!result)
					result = *a - *b;
				++a; ++b;
			}
			while(*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9');

			if(*a >= '0' && *a <= '9')
				return 1;
			else if(*b >= '0' && *b <= '9')
				return -1;
			else if(result)
				return result;
		}

		if(*a != *b)
			break;
	}
	return *a - *b;
}

const char *str_find_nocase(const char *haystack, const char *needle)
{
	while(*haystack) /* native implementation */
	{
		const char *a = haystack;
		const char *b = needle;
		while(*a && *b && tolower(*a) == tolower(*b))
		{
			a++;
			b++;
		}
		if(!(*b))
			return haystack;
		haystack++;
	}

	return 0;
}


const char *str_find(const char *haystack, const char *needle)
{
	while(*haystack) /* native implementation */
	{
		const char *a = haystack;
		const char *b = needle;
		while(*a && *b && *a == *b)
		{
			a++;
			b++;
		}
		if(!(*b))
			return haystack;
		haystack++;
	}

	return 0;
}

void str_hex(char *dst, int dst_size, const void *data, int data_size)
{
	static const char hex[] = "0123456789ABCDEF";
	int b;

	for(b = 0; b < data_size && b < dst_size/4-4; b++)
	{
		dst[b*3] = hex[((const unsigned char *)data)[b]>>4];
		dst[b*3+1] = hex[((const unsigned char *)data)[b]&0xf];
		dst[b*3+2] = ' ';
		dst[b*3+3] = 0;
	}
}

void str_timestamp_ex(time_t time_data, char *buffer, int buffer_size, const char *format)
{
	struct tm *time_info;

	time_info = localtime(&time_data);
	strftime(buffer, buffer_size, format, time_info);
	buffer[buffer_size-1] = 0;	/* assure null termination */
}

void str_timestamp(char *buffer, int buffer_size)
{
	time_t time_data;
	time(&time_data);
	str_timestamp_ex(time_data, buffer, buffer_size, "%Y-%m-{}_%H-%M-{}");
}

int mem_comp(const void *a, const void *b, int size)
{
	return memcmp(a,b,size);
}

const MEMSTATS *mem_stats()
{
	return &memory_stats;
}

void net_stats(NETSTATS *stats_inout)
{
	*stats_inout = network_stats;
}

void gui_messagebox(const char *title, const char *message)
{
#if defined(CONF_PLATFORM_MACOSX)
	DialogRef theItem;
	DialogItemIndex itemIndex;

	/* FIXME: really needed? can we rely on glfw? */
	/* HACK - get events without a bundle */
	ProcessSerialNumber psn;
	GetCurrentProcess(&psn);
	TransformProcessType(&psn,kProcessTransformToForegroundApplication);
	SetFrontProcess(&psn);
	/* END HACK */

	CreateStandardAlert(kAlertStopAlert,
			CFStringCreateWithCString(NULL, title, kCFStringEncodingASCII),
			CFStringCreateWithCString(NULL, message, kCFStringEncodingASCII),
			NULL,
			&theItem);

	RunStandardAlert(theItem, NULL, &itemIndex);
#elif defined(CONF_FAMILY_UNIX)
	static char cmd[1024];
	int err;
	/* use xmessage which is available on nearly every X11 system */
	snprintf(cmd, sizeof(cmd), "xmessage -center -title '{}' '{}'",
		title,
		message);

	err = system(cmd);
	log_msgf("gui/msgbox", "result = {}", err);
#elif defined(CONF_FAMILY_WINDOWS)
	MessageBox(NULL,
		message,
		title,
		MB_ICONEXCLAMATION | MB_OK);
#else
	/* this is not critical */
	#warning not implemented
#endif
}

int str_isspace(char c) { return c == ' ' || c == '\n' || c == '\t'; }

char str_uppercase(char c)
{
	if(c >= 'a' && c <= 'z')
		return 'A' + (c-'a');
	return c;
}

int str_toint(const char *str) { return atoi(str); }
int str_toint_base(const char *str, int base) { return strtol(str, NULL, base); }
float str_tofloat(const char *str) { return atof(str); }

int str_utf8_is_confusable(int smaller, int bigger)
{
	switch(smaller)
	{
	case 0x0020: return bigger == 0x00A0 || bigger == 0x1680 || bigger == 0x2000 || bigger == 0x2001 || bigger == 0x2002 || bigger == 0x2003 || bigger == 0x2004 || bigger == 0x2005 || bigger == 0x2006 || bigger == 0x2007 || bigger == 0x2008 || bigger == 0x2009 || bigger == 0x200A || bigger == 0x2028 || bigger == 0x2029 || bigger == 0x202F || bigger == 0x205F;
	case 0x0021: return bigger == 0x01C3 || bigger == 0x2D51 || bigger == 0xFF01;
	case 0x0022: return bigger == 0x02BA || bigger == 0x02DD || bigger == 0x02EE || bigger == 0x02F6 || bigger == 0x05F2 || bigger == 0x05F4 || bigger == 0x1CD3 || bigger == 0x201C || bigger == 0x201D || bigger == 0x201F || bigger == 0x2033 || bigger == 0x2036 || bigger == 0x3003 || bigger == 0xFF02;
	case 0x0025: return bigger == 0x066A || bigger == 0x2052;
	case 0x0026: return bigger == 0xA778;
	case 0x0027: return bigger == 0x0060 || bigger == 0x00B4 || bigger == 0x02B9 || bigger == 0x02BB || bigger == 0x02BC || bigger == 0x02BD || bigger == 0x02BE || bigger == 0x02C8 || bigger == 0x02CA || bigger == 0x02CB || bigger == 0x02F4 || bigger == 0x0374 || bigger == 0x0384 || bigger == 0x055A || bigger == 0x055D || bigger == 0x05D9 || bigger == 0x05F3 || bigger == 0x07F4 || bigger == 0x07F5 || bigger == 0x144A || bigger == 0x16CC || bigger == 0x1FBD || bigger == 0x1FBF || bigger == 0x1FEF || bigger == 0x1FFD || bigger == 0x1FFE || bigger == 0x2018 || bigger == 0x2019 || bigger == 0x201B || bigger == 0x2032 || bigger == 0x2035 || bigger == 0xA78C || bigger == 0xFF07 || bigger == 0xFF40;
	case 0x0028: return bigger == 0x2768 || bigger == 0x2772 || bigger == 0x3014 || bigger == 0xFD3E || bigger == 0xFF3B;
	case 0x0029: return bigger == 0x2769 || bigger == 0x2773 || bigger == 0x3015 || bigger == 0xFD3F || bigger == 0xFF3D;
	case 0x002A: return bigger == 0x066D || bigger == 0x204E || bigger == 0x2217 || bigger == 0x1031F;
	case 0x002B: return bigger == 0x16ED || bigger == 0x2795 || bigger == 0x1029B;
	case 0x002C: return bigger == 0x00B8 || bigger == 0x060D || bigger == 0x066B || bigger == 0x201A || bigger == 0xA4F9;
	case 0x002D: return bigger == 0x02D7 || bigger == 0x06D4 || bigger == 0x2010 || bigger == 0x2011 || bigger == 0x2012 || bigger == 0x2013 || bigger == 0x2043 || bigger == 0x2212 || bigger == 0x2796 || bigger == 0x2CBA || bigger == 0xFE58;
	case 0x002E: return bigger == 0x0660 || bigger == 0x06F0 || bigger == 0x0701 || bigger == 0x0702 || bigger == 0x2024 || bigger == 0xA4F8 || bigger == 0xA60E || bigger == 0x10A50 || bigger == 0x1D16D;
	case 0x002F: return bigger == 0x1735 || bigger == 0x2041 || bigger == 0x2044 || bigger == 0x2215 || bigger == 0x2571 || bigger == 0x27CB || bigger == 0x29F8 || bigger == 0x2CC6 || bigger == 0x2F03 || bigger == 0x3033 || bigger == 0x31D3 || bigger == 0x4E3F;
	case 0x0030: return bigger == 0x004F;
	case 0x0031: return bigger == 0x006C;
	case 0x0032: return bigger == 0x01A7 || bigger == 0x03E8 || bigger == 0x14BF || bigger == 0xA644 || bigger == 0xA75A || bigger == 0x1D7D0 || bigger == 0x1D7DA || bigger == 0x1D7E4 || bigger == 0x1D7EE || bigger == 0x1D7F8;
	case 0x0033: return bigger == 0x01B7 || bigger == 0x021C || bigger == 0x0417 || bigger == 0x04E0 || bigger == 0x2CCC || bigger == 0xA76A || bigger == 0xA7AB || bigger == 0x118CA || bigger == 0x1D7D1 || bigger == 0x1D7DB || bigger == 0x1D7E5 || bigger == 0x1D7EF || bigger == 0x1D7F9;
	case 0x0034: return bigger == 0x13CE || bigger == 0x118AF || bigger == 0x1D7D2 || bigger == 0x1D7DC || bigger == 0x1D7E6 || bigger == 0x1D7F0 || bigger == 0x1D7FA;
	case 0x0035: return bigger == 0x01BC || bigger == 0x118BB || bigger == 0x1D7D3 || bigger == 0x1D7DD || bigger == 0x1D7E7 || bigger == 0x1D7F1 || bigger == 0x1D7FB;
	case 0x0036: return bigger == 0x0431 || bigger == 0x13EE || bigger == 0x2CD2 || bigger == 0x118D5 || bigger == 0x1D7D4 || bigger == 0x1D7DE || bigger == 0x1D7E8 || bigger == 0x1D7F2 || bigger == 0x1D7FC;
	case 0x0037: return bigger == 0x118C6 || bigger == 0x1D7D5 || bigger == 0x1D7DF || bigger == 0x1D7E9 || bigger == 0x1D7F3 || bigger == 0x1D7FD;
	case 0x0038: return bigger == 0x0222 || bigger == 0x0223 || bigger == 0x09EA || bigger == 0x0A6A || bigger == 0x0B03 || bigger == 0x1031A || bigger == 0x1D7D6 || bigger == 0x1D7E0 || bigger == 0x1D7EA || bigger == 0x1D7F4 || bigger == 0x1D7FE || bigger == 0x1E8CB;
	case 0x0039: return bigger == 0x09ED || bigger == 0x0A67 || bigger == 0x0B68 || bigger == 0x2CCA || bigger == 0xA76E || bigger == 0x118AC || bigger == 0x118CC || bigger == 0x118D6 || bigger == 0x1D7D7 || bigger == 0x1D7E1 || bigger == 0x1D7EB || bigger == 0x1D7F5 || bigger == 0x1D7FF;
	case 0x003A: return bigger == 0x02D0 || bigger == 0x02F8 || bigger == 0x0589 || bigger == 0x05C3 || bigger == 0x0703 || bigger == 0x0704 || bigger == 0x0903 || bigger == 0x0A83 || bigger == 0x16EC || bigger == 0x1803 || bigger == 0x1809 || bigger == 0x205A || bigger == 0x2236 || bigger == 0xA4FD || bigger == 0xA789 || bigger == 0xFE30 || bigger == 0xFF1A;
	case 0x003B: return bigger == 0x037E;
	case 0x003C: return bigger == 0x02C2 || bigger == 0x1438 || bigger == 0x16B2 || bigger == 0x2039 || bigger == 0x276E;
	case 0x003D: return bigger == 0x1400 || bigger == 0x2E40 || bigger == 0x30A0 || bigger == 0xA4FF;
	case 0x003E: return bigger == 0x02C3 || bigger == 0x1433 || bigger == 0x203A || bigger == 0x276F;
	case 0x003F: return bigger == 0x0241 || bigger == 0x0294 || bigger == 0x097D || bigger == 0x13AE;
	case 0x0041: return bigger == 0x0391 || bigger == 0x0410 || bigger == 0x13AA || bigger == 0x15C5 || bigger == 0x1D00 || bigger == 0xA4EE || bigger == 0xFF21 || bigger == 0x102A0 || bigger == 0x1D400 || bigger == 0x1D434 || bigger == 0x1D468 || bigger == 0x1D49C || bigger == 0x1D4D0 || bigger == 0x1D504 || bigger == 0x1D538 || bigger == 0x1D56C || bigger == 0x1D5A0 || bigger == 0x1D5D4 || bigger == 0x1D608 || bigger == 0x1D63C || bigger == 0x1D670 || bigger == 0x1D6A8 || bigger == 0x1D6E2 || bigger == 0x1D71C || bigger == 0x1D756 || bigger == 0x1D790;
	case 0x0042: return bigger == 0x0392 || bigger == 0x0412 || bigger == 0x13F4 || bigger == 0x15F7 || bigger == 0x212C || bigger == 0xA4D0 || bigger == 0xA7B4 || bigger == 0xFF22 || bigger == 0x10282 || bigger == 0x102A1 || bigger == 0x10301 || bigger == 0x1D401 || bigger == 0x1D435 || bigger == 0x1D469 || bigger == 0x1D4D1 || bigger == 0x1D505 || bigger == 0x1D539 || bigger == 0x1D56D || bigger == 0x1D5A1 || bigger == 0x1D5D5 || bigger == 0x1D609 || bigger == 0x1D63D || bigger == 0x1D671 || bigger == 0x1D6A9 || bigger == 0x1D6E3 || bigger == 0x1D71D || bigger == 0x1D757 || bigger == 0x1D791;
	case 0x0043: return bigger == 0x03F9 || bigger == 0x0421 || bigger == 0x13DF || bigger == 0x2102 || bigger == 0x212D || bigger == 0x216D || bigger == 0x2CA4 || bigger == 0xA4DA || bigger == 0xFF23 || bigger == 0x102A2 || bigger == 0x10302 || bigger == 0x10415 || bigger == 0x1051C || bigger == 0x118E9 || bigger == 0x118F2 || bigger == 0x1D402 || bigger == 0x1D436 || bigger == 0x1D46A || bigger == 0x1D49E || bigger == 0x1D4D2 || bigger == 0x1D56E || bigger == 0x1D5A2 || bigger == 0x1D5D6 || bigger == 0x1D60A || bigger == 0x1D63E || bigger == 0x1D672 || bigger == 0x1F74C;
	case 0x0044: return bigger == 0x13A0 || bigger == 0x15DE || bigger == 0x15EA || bigger == 0x2145 || bigger == 0x216E || bigger == 0xA4D3 || bigger == 0x1D403 || bigger == 0x1D437 || bigger == 0x1D46B || bigger == 0x1D49F || bigger == 0x1D4D3 || bigger == 0x1D507 || bigger == 0x1D53B || bigger == 0x1D56F || bigger == 0x1D5A3 || bigger == 0x1D5D7 || bigger == 0x1D60B || bigger == 0x1D63F || bigger == 0x1D673;
	case 0x0045: return bigger == 0x0395 || bigger == 0x0415 || bigger == 0x13AC || bigger == 0x2130 || bigger == 0x22FF || bigger == 0x2D39 || bigger == 0xA4F0 || bigger == 0xFF25 || bigger == 0x10286 || bigger == 0x118A6 || bigger == 0x118AE || bigger == 0x1D404 || bigger == 0x1D438 || bigger == 0x1D46C || bigger == 0x1D4D4 || bigger == 0x1D508 || bigger == 0x1D53C || bigger == 0x1D570 || bigger == 0x1D5A4 || bigger == 0x1D5D8 || bigger == 0x1D60C || bigger == 0x1D640 || bigger == 0x1D674 || bigger == 0x1D6AC || bigger == 0x1D6E6 || bigger == 0x1D720 || bigger == 0x1D75A || bigger == 0x1D794;
	case 0x0046: return bigger == 0x03DC || bigger == 0x15B4 || bigger == 0x2131 || bigger == 0xA4DD || bigger == 0xA798 || bigger == 0x10287 || bigger == 0x102A5 || bigger == 0x10525 || bigger == 0x118A2 || bigger == 0x118C2 || bigger == 0x1D405 || bigger == 0x1D439 || bigger == 0x1D46D || bigger == 0x1D4D5 || bigger == 0x1D509 || bigger == 0x1D53D || bigger == 0x1D571 || bigger == 0x1D5A5 || bigger == 0x1D5D9 || bigger == 0x1D60D || bigger == 0x1D641 || bigger == 0x1D675 || bigger == 0x1D7CA;
	case 0x0047: return bigger == 0x050C || bigger == 0x13C0 || bigger == 0x13F3 || bigger == 0xA4D6 || bigger == 0x1D406 || bigger == 0x1D43A || bigger == 0x1D46E || bigger == 0x1D4A2 || bigger == 0x1D4D6 || bigger == 0x1D50A || bigger == 0x1D53E || bigger == 0x1D572 || bigger == 0x1D5A6 || bigger == 0x1D5DA || bigger == 0x1D60E || bigger == 0x1D642 || bigger == 0x1D676;
	case 0x0048: return bigger == 0x0397 || bigger == 0x041D || bigger == 0x13BB || bigger == 0x157C || bigger == 0x210B || bigger == 0x210C || bigger == 0x210D || bigger == 0x2C8E || bigger == 0xA4E7 || bigger == 0xFF28 || bigger == 0x102CF || bigger == 0x1D407 || bigger == 0x1D43B || bigger == 0x1D46F || bigger == 0x1D4D7 || bigger == 0x1D573 || bigger == 0x1D5A7 || bigger == 0x1D5DB || bigger == 0x1D60F || bigger == 0x1D643 || bigger == 0x1D677 || bigger == 0x1D6AE || bigger == 0x1D6E8 || bigger == 0x1D722 || bigger == 0x1D75C || bigger == 0x1D796;
	case 0x0049: return bigger == 0x006C;
	case 0x004A: return bigger == 0x037F || bigger == 0x0408 || bigger == 0x13AB || bigger == 0x148D || bigger == 0xA4D9 || bigger == 0xA7B2 || bigger == 0xFF2A || bigger == 0x1D409 || bigger == 0x1D43D || bigger == 0x1D471 || bigger == 0x1D4A5 || bigger == 0x1D4D9 || bigger == 0x1D50D || bigger == 0x1D541 || bigger == 0x1D575 || bigger == 0x1D5A9 || bigger == 0x1D5DD || bigger == 0x1D611 || bigger == 0x1D645 || bigger == 0x1D679;
	case 0x004B: return bigger == 0x039A || bigger == 0x041A || bigger == 0x13E6 || bigger == 0x16D5 || bigger == 0x212A || bigger == 0x2C94 || bigger == 0xA4D7 || bigger == 0xFF2B || bigger == 0x10518 || bigger == 0x1D40A || bigger == 0x1D43E || bigger == 0x1D472 || bigger == 0x1D4A6 || bigger == 0x1D4DA || bigger == 0x1D50E || bigger == 0x1D542 || bigger == 0x1D576 || bigger == 0x1D5AA || bigger == 0x1D5DE || bigger == 0x1D612 || bigger == 0x1D646 || bigger == 0x1D67A || bigger == 0x1D6B1 || bigger == 0x1D6EB || bigger == 0x1D725 || bigger == 0x1D75F || bigger == 0x1D799;
	case 0x004C: return bigger == 0x13DE || bigger == 0x14AA || bigger == 0x2112 || bigger == 0x216C || bigger == 0x2CD0 || bigger == 0xA4E1 || bigger == 0x1041B || bigger == 0x10526 || bigger == 0x118A3 || bigger == 0x118B2 || bigger == 0x1D40B || bigger == 0x1D43F || bigger == 0x1D473 || bigger == 0x1D4DB || bigger == 0x1D50F || bigger == 0x1D543 || bigger == 0x1D577 || bigger == 0x1D5AB || bigger == 0x1D5DF || bigger == 0x1D613 || bigger == 0x1D647 || bigger == 0x1D67B;
	case 0x004D: return bigger == 0x039C || bigger == 0x03FA || bigger == 0x041C || bigger == 0x13B7 || bigger == 0x15F0 || bigger == 0x16D6 || bigger == 0x2133 || bigger == 0x216F || bigger == 0x2C98 || bigger == 0xA4DF || bigger == 0xFF2D || bigger == 0x102B0 || bigger == 0x10311 || bigger == 0x1D40C || bigger == 0x1D440 || bigger == 0x1D474 || bigger == 0x1D4DC || bigger == 0x1D510 || bigger == 0x1D544 || bigger == 0x1D578 || bigger == 0x1D5AC || bigger == 0x1D5E0 || bigger == 0x1D614 || bigger == 0x1D648 || bigger == 0x1D67C || bigger == 0x1D6B3 || bigger == 0x1D6ED || bigger == 0x1D727 || bigger == 0x1D761 || bigger == 0x1D79B;
	case 0x004E: return bigger == 0x039D || bigger == 0x2115 || bigger == 0x2C9A || bigger == 0xA4E0 || bigger == 0xFF2E || bigger == 0x10513 || bigger == 0x1D40D || bigger == 0x1D441 || bigger == 0x1D475 || bigger == 0x1D4A9 || bigger == 0x1D4DD || bigger == 0x1D511 || bigger == 0x1D579 || bigger == 0x1D5AD || bigger == 0x1D5E1 || bigger == 0x1D615 || bigger == 0x1D649 || bigger == 0x1D67D || bigger == 0x1D6B4 || bigger == 0x1D6EE || bigger == 0x1D728 || bigger == 0x1D762 || bigger == 0x1D79C;
	case 0x004F: return bigger == 0x039F || bigger == 0x041E || bigger == 0x0555 || bigger == 0x07C0 || bigger == 0x09E6 || bigger == 0x0B20 || bigger == 0x0B66 || bigger == 0x0D20 || bigger == 0x2C9E || bigger == 0x2D54 || bigger == 0x3007 || bigger == 0xA4F3 || bigger == 0xFF2F || bigger == 0x10292 || bigger == 0x102AB || bigger == 0x10404 || bigger == 0x10516 || bigger == 0x114D0 || bigger == 0x118B5 || bigger == 0x118E0 || bigger == 0x1D40E || bigger == 0x1D442 || bigger == 0x1D476 || bigger == 0x1D4AA || bigger == 0x1D4DE || bigger == 0x1D512 || bigger == 0x1D546 || bigger == 0x1D57A || bigger == 0x1D5AE || bigger == 0x1D5E2 || bigger == 0x1D616 || bigger == 0x1D64A || bigger == 0x1D67E || bigger == 0x1D6B6 || bigger == 0x1D6F0 || bigger == 0x1D72A || bigger == 0x1D764 || bigger == 0x1D79E || bigger == 0x1D7CE || bigger == 0x1D7D8 || bigger == 0x1D7E2 || bigger == 0x1D7EC || bigger == 0x1D7F6;
	case 0x0050: return bigger == 0x03A1 || bigger == 0x0420 || bigger == 0x13E2 || bigger == 0x146D || bigger == 0x2119 || bigger == 0x2CA2 || bigger == 0xA4D1 || bigger == 0xFF30 || bigger == 0x10295 || bigger == 0x1D40F || bigger == 0x1D443 || bigger == 0x1D477 || bigger == 0x1D4AB || bigger == 0x1D4DF || bigger == 0x1D513 || bigger == 0x1D57B || bigger == 0x1D5AF || bigger == 0x1D5E3 || bigger == 0x1D617 || bigger == 0x1D64B || bigger == 0x1D67F || bigger == 0x1D6B8 || bigger == 0x1D6F2 || bigger == 0x1D72C || bigger == 0x1D766 || bigger == 0x1D7A0;
	case 0x0051: return bigger == 0x211A || bigger == 0x2D55 || bigger == 0x1D410 || bigger == 0x1D444 || bigger == 0x1D478 || bigger == 0x1D4AC || bigger == 0x1D4E0 || bigger == 0x1D514 || bigger == 0x1D57C || bigger == 0x1D5B0 || bigger == 0x1D5E4 || bigger == 0x1D618 || bigger == 0x1D64C || bigger == 0x1D680;
	case 0x0052: return bigger == 0x01A6 || bigger == 0x13A1 || bigger == 0x13D2 || bigger == 0x1587 || bigger == 0x211B || bigger == 0x211C || bigger == 0x211D || bigger == 0xA4E3 || bigger == 0x1D411 || bigger == 0x1D445 || bigger == 0x1D479 || bigger == 0x1D4E1 || bigger == 0x1D57D || bigger == 0x1D5B1 || bigger == 0x1D5E5 || bigger == 0x1D619 || bigger == 0x1D64D || bigger == 0x1D681;
	case 0x0053: return bigger == 0x0405 || bigger == 0x054F || bigger == 0x13D5 || bigger == 0x13DA || bigger == 0xA4E2 || bigger == 0xFF33 || bigger == 0x10296 || bigger == 0x10420 || bigger == 0x1D412 || bigger == 0x1D446 || bigger == 0x1D47A || bigger == 0x1D4AE || bigger == 0x1D4E2 || bigger == 0x1D516 || bigger == 0x1D54A || bigger == 0x1D57E || bigger == 0x1D5B2 || bigger == 0x1D5E6 || bigger == 0x1D61A || bigger == 0x1D64E || bigger == 0x1D682;
	case 0x0054: return bigger == 0x03A4 || bigger == 0x0422 || bigger == 0x13A2 || bigger == 0x22A4 || bigger == 0x27D9 || bigger == 0x2CA6 || bigger == 0xA4D4 || bigger == 0xFF34 || bigger == 0x10297 || bigger == 0x102B1 || bigger == 0x10315 || bigger == 0x118BC || bigger == 0x1D413 || bigger == 0x1D447 || bigger == 0x1D47B || bigger == 0x1D4AF || bigger == 0x1D4E3 || bigger == 0x1D517 || bigger == 0x1D54B || bigger == 0x1D57F || bigger == 0x1D5B3 || bigger == 0x1D5E7 || bigger == 0x1D61B || bigger == 0x1D64F || bigger == 0x1D683 || bigger == 0x1D6BB || bigger == 0x1D6F5 || bigger == 0x1D72F || bigger == 0x1D769 || bigger == 0x1D7A3 || bigger == 0x1F768;
	case 0x0055: return bigger == 0x054D || bigger == 0x144C || bigger == 0x222A || bigger == 0x22C3 || bigger == 0xA4F4 || bigger == 0x118B8 || bigger == 0x1D414 || bigger == 0x1D448 || bigger == 0x1D47C || bigger == 0x1D4B0 || bigger == 0x1D4E4 || bigger == 0x1D518 || bigger == 0x1D54C || bigger == 0x1D580 || bigger == 0x1D5B4 || bigger == 0x1D5E8 || bigger == 0x1D61C || bigger == 0x1D650 || bigger == 0x1D684;
	case 0x0056: return bigger == 0x0474 || bigger == 0x0667 || bigger == 0x06F7 || bigger == 0x13D9 || bigger == 0x142F || bigger == 0x2164 || bigger == 0x2D38 || bigger == 0xA4E6 || bigger == 0x1051D || bigger == 0x118A0 || bigger == 0x1D415 || bigger == 0x1D449 || bigger == 0x1D47D || bigger == 0x1D4B1 || bigger == 0x1D4E5 || bigger == 0x1D519 || bigger == 0x1D54D || bigger == 0x1D581 || bigger == 0x1D5B5 || bigger == 0x1D5E9 || bigger == 0x1D61D || bigger == 0x1D651 || bigger == 0x1D685;
	case 0x0057: return bigger == 0x051C || bigger == 0x13B3 || bigger == 0x13D4 || bigger == 0xA4EA || bigger == 0x118E6 || bigger == 0x118EF || bigger == 0x1D416 || bigger == 0x1D44A || bigger == 0x1D47E || bigger == 0x1D4B2 || bigger == 0x1D4E6 || bigger == 0x1D51A || bigger == 0x1D54E || bigger == 0x1D582 || bigger == 0x1D5B6 || bigger == 0x1D5EA || bigger == 0x1D61E || bigger == 0x1D652 || bigger == 0x1D686;
	case 0x0058: return bigger == 0x03A7 || bigger == 0x0425 || bigger == 0x166D || bigger == 0x16B7 || bigger == 0x2169 || bigger == 0x2573 || bigger == 0x2CAC || bigger == 0x2D5D || bigger == 0xA4EB || bigger == 0xA7B3 || bigger == 0xFF38 || bigger == 0x10290 || bigger == 0x102B4 || bigger == 0x10317 || bigger == 0x10322 || bigger == 0x10527 || bigger == 0x118EC || bigger == 0x1D417 || bigger == 0x1D44B || bigger == 0x1D47F || bigger == 0x1D4B3 || bigger == 0x1D4E7 || bigger == 0x1D51B || bigger == 0x1D54F || bigger == 0x1D583 || bigger == 0x1D5B7 || bigger == 0x1D5EB || bigger == 0x1D61F || bigger == 0x1D653 || bigger == 0x1D687 || bigger == 0x1D6BE || bigger == 0x1D6F8 || bigger == 0x1D732 || bigger == 0x1D76C || bigger == 0x1D7A6;
	case 0x0059: return bigger == 0x03A5 || bigger == 0x03D2 || bigger == 0x04AE || bigger == 0x13A9 || bigger == 0x13BD || bigger == 0x2CA8 || bigger == 0xA4EC || bigger == 0xFF39 || bigger == 0x102B2 || bigger == 0x118A4 || bigger == 0x1D418 || bigger == 0x1D44C || bigger == 0x1D480 || bigger == 0x1D4B4 || bigger == 0x1D4E8 || bigger == 0x1D51C || bigger == 0x1D550 || bigger == 0x1D584 || bigger == 0x1D5B8 || bigger == 0x1D5EC || bigger == 0x1D620 || bigger == 0x1D654 || bigger == 0x1D688 || bigger == 0x1D6BC || bigger == 0x1D6F6 || bigger == 0x1D730 || bigger == 0x1D76A || bigger == 0x1D7A4;
	case 0x005A: return bigger == 0x0396 || bigger == 0x13C3 || bigger == 0x2124 || bigger == 0x2128 || bigger == 0xA4DC || bigger == 0xFF3A || bigger == 0x102F5 || bigger == 0x118A9 || bigger == 0x118E5 || bigger == 0x1D419 || bigger == 0x1D44D || bigger == 0x1D481 || bigger == 0x1D4B5 || bigger == 0x1D4E9 || bigger == 0x1D585 || bigger == 0x1D5B9 || bigger == 0x1D5ED || bigger == 0x1D621 || bigger == 0x1D655 || bigger == 0x1D689 || bigger == 0x1D6AD || bigger == 0x1D6E7 || bigger == 0x1D721 || bigger == 0x1D75B || bigger == 0x1D795;
	case 0x005C: return bigger == 0x2216 || bigger == 0x27CD || bigger == 0x29F5 || bigger == 0x29F9 || bigger == 0x2F02 || bigger == 0x31D4 || bigger == 0x4E36 || bigger == 0xFE68 || bigger == 0xFF3C;
	case 0x005E: return bigger == 0x02C4 || bigger == 0x02C6;
	case 0x005F: return bigger == 0x07FA || bigger == 0xFE4D || bigger == 0xFE4E || bigger == 0xFE4F;
	case 0x0061: return bigger == 0x0251 || bigger == 0x03B1 || bigger == 0x0430 || bigger == 0x237A || bigger == 0xFF41 || bigger == 0x1D41A || bigger == 0x1D44E || bigger == 0x1D482 || bigger == 0x1D4B6 || bigger == 0x1D4EA || bigger == 0x1D51E || bigger == 0x1D552 || bigger == 0x1D586 || bigger == 0x1D5BA || bigger == 0x1D5EE || bigger == 0x1D622 || bigger == 0x1D656 || bigger == 0x1D68A || bigger == 0x1D6C2 || bigger == 0x1D6FC || bigger == 0x1D736 || bigger == 0x1D770 || bigger == 0x1D7AA;
	case 0x0062: return bigger == 0x0184 || bigger == 0x042C || bigger == 0x13CF || bigger == 0x15AF || bigger == 0x1D41B || bigger == 0x1D44F || bigger == 0x1D483 || bigger == 0x1D4B7 || bigger == 0x1D4EB || bigger == 0x1D51F || bigger == 0x1D553 || bigger == 0x1D587 || bigger == 0x1D5BB || bigger == 0x1D5EF || bigger == 0x1D623 || bigger == 0x1D657 || bigger == 0x1D68B;
	case 0x0063: return bigger == 0x03F2 || bigger == 0x0441 || bigger == 0x1D04 || bigger == 0x217D || bigger == 0x2CA5 || bigger == 0xFF43 || bigger == 0x1043D || bigger == 0x1D41C || bigger == 0x1D450 || bigger == 0x1D484 || bigger == 0x1D4B8 || bigger == 0x1D4EC || bigger == 0x1D520 || bigger == 0x1D554 || bigger == 0x1D588 || bigger == 0x1D5BC || bigger == 0x1D5F0 || bigger == 0x1D624 || bigger == 0x1D658 || bigger == 0x1D68C;
	case 0x0064: return bigger == 0x0501 || bigger == 0x13E7 || bigger == 0x146F || bigger == 0x2146 || bigger == 0x217E || bigger == 0xA4D2 || bigger == 0x1D41D || bigger == 0x1D451 || bigger == 0x1D485 || bigger == 0x1D4B9 || bigger == 0x1D4ED || bigger == 0x1D521 || bigger == 0x1D555 || bigger == 0x1D589 || bigger == 0x1D5BD || bigger == 0x1D5F1 || bigger == 0x1D625 || bigger == 0x1D659 || bigger == 0x1D68D;
	case 0x0065: return bigger == 0x0435 || bigger == 0x04BD || bigger == 0x212E || bigger == 0x212F || bigger == 0x2147 || bigger == 0xAB32 || bigger == 0xFF45 || bigger == 0x1D41E || bigger == 0x1D452 || bigger == 0x1D486 || bigger == 0x1D4EE || bigger == 0x1D522 || bigger == 0x1D556 || bigger == 0x1D58A || bigger == 0x1D5BE || bigger == 0x1D5F2 || bigger == 0x1D626 || bigger == 0x1D65A || bigger == 0x1D68E;
	case 0x0066: return bigger == 0x017F || bigger == 0x0584 || bigger == 0x1E9D || bigger == 0xA799 || bigger == 0xAB35 || bigger == 0x1D41F || bigger == 0x1D453 || bigger == 0x1D487 || bigger == 0x1D4BB || bigger == 0x1D4EF || bigger == 0x1D523 || bigger == 0x1D557 || bigger == 0x1D58B || bigger == 0x1D5BF || bigger == 0x1D5F3 || bigger == 0x1D627 || bigger == 0x1D65B || bigger == 0x1D68F;
	case 0x0067: return bigger == 0x018D || bigger == 0x0261 || bigger == 0x0581 || bigger == 0x1D83 || bigger == 0x210A || bigger == 0xFF47 || bigger == 0x1D420 || bigger == 0x1D454 || bigger == 0x1D488 || bigger == 0x1D4F0 || bigger == 0x1D524 || bigger == 0x1D558 || bigger == 0x1D58C || bigger == 0x1D5C0 || bigger == 0x1D5F4 || bigger == 0x1D628 || bigger == 0x1D65C || bigger == 0x1D690;
	case 0x0068: return bigger == 0x04BB || bigger == 0x0570 || bigger == 0x13C2 || bigger == 0x210E || bigger == 0xFF48 || bigger == 0x1D421 || bigger == 0x1D489 || bigger == 0x1D4BD || bigger == 0x1D4F1 || bigger == 0x1D525 || bigger == 0x1D559 || bigger == 0x1D58D || bigger == 0x1D5C1 || bigger == 0x1D5F5 || bigger == 0x1D629 || bigger == 0x1D65D || bigger == 0x1D691;
	case 0x0069: return bigger == 0x0131 || bigger == 0x0269 || bigger == 0x026A || bigger == 0x02DB || bigger == 0x037A || bigger == 0x03B9 || bigger == 0x0456 || bigger == 0x04CF || bigger == 0x13A5 || bigger == 0x1FBE || bigger == 0x2139 || bigger == 0x2148 || bigger == 0x2170 || bigger == 0x2373 || bigger == 0xA647 || bigger == 0xFF49 || bigger == 0x118C3 || bigger == 0x1D422 || bigger == 0x1D456 || bigger == 0x1D48A || bigger == 0x1D4BE || bigger == 0x1D4F2 || bigger == 0x1D526 || bigger == 0x1D55A || bigger == 0x1D58E || bigger == 0x1D5C2 || bigger == 0x1D5F6 || bigger == 0x1D62A || bigger == 0x1D65E || bigger == 0x1D692 || bigger == 0x1D6A4 || bigger == 0x1D6CA || bigger == 0x1D704 || bigger == 0x1D73E || bigger == 0x1D778 || bigger == 0x1D7B2;
	case 0x006A: return bigger == 0x03F3 || bigger == 0x0458 || bigger == 0x2149 || bigger == 0xFF4A || bigger == 0x1D423 || bigger == 0x1D457 || bigger == 0x1D48B || bigger == 0x1D4BF || bigger == 0x1D4F3 || bigger == 0x1D527 || bigger == 0x1D55B || bigger == 0x1D58F || bigger == 0x1D5C3 || bigger == 0x1D5F7 || bigger == 0x1D62B || bigger == 0x1D65F || bigger == 0x1D693;
	case 0x006B: return bigger == 0x0138 || bigger == 0x03BA || bigger == 0x03F0 || bigger == 0x043A || bigger == 0x1D0B || bigger == 0x2C95 || bigger == 0x1D424 || bigger == 0x1D458 || bigger == 0x1D48C || bigger == 0x1D4C0 || bigger == 0x1D4F4 || bigger == 0x1D528 || bigger == 0x1D55C || bigger == 0x1D590 || bigger == 0x1D5C4 || bigger == 0x1D5F8 || bigger == 0x1D62C || bigger == 0x1D660 || bigger == 0x1D694 || bigger == 0x1D6CB || bigger == 0x1D6DE || bigger == 0x1D705 || bigger == 0x1D718 || bigger == 0x1D73F || bigger == 0x1D752 || bigger == 0x1D779 || bigger == 0x1D78C || bigger == 0x1D7B3 || bigger == 0x1D7C6;
	case 0x006C: return bigger == 0x007C || bigger == 0x0196 || bigger == 0x01C0 || bigger == 0x0399 || bigger == 0x0406 || bigger == 0x04C0 || bigger == 0x05C0 || bigger == 0x05D5 || bigger == 0x05DF || bigger == 0x0627 || bigger == 0x0661 || bigger == 0x06F1 || bigger == 0x07CA || bigger == 0x16C1 || bigger == 0x2110 || bigger == 0x2111 || bigger == 0x2113 || bigger == 0x2160 || bigger == 0x217C || bigger == 0x2223 || bigger == 0x2C92 || bigger == 0x2D4F || bigger == 0xA4F2 || bigger == 0xFE8D || bigger == 0xFE8E || bigger == 0xFF29 || bigger == 0xFF4C || bigger == 0xFFE8 || bigger == 0x1028A || bigger == 0x10309 || bigger == 0x10320 || bigger == 0x1D408 || bigger == 0x1D425 || bigger == 0x1D43C || bigger == 0x1D459 || bigger == 0x1D470 || bigger == 0x1D48D || bigger == 0x1D4C1 || bigger == 0x1D4D8 || bigger == 0x1D4F5 || bigger == 0x1D529 || bigger == 0x1D540 || bigger == 0x1D55D || bigger == 0x1D574 || bigger == 0x1D591 || bigger == 0x1D5A8 || bigger == 0x1D5C5 || bigger == 0x1D5DC || bigger == 0x1D5F9 || bigger == 0x1D610 || bigger == 0x1D62D || bigger == 0x1D644 || bigger == 0x1D661 || bigger == 0x1D678 || bigger == 0x1D695 || bigger == 0x1D6B0 || bigger == 0x1D6EA || bigger == 0x1D724 || bigger == 0x1D75E || bigger == 0x1D798 || bigger == 0x1D7CF || bigger == 0x1D7D9 || bigger == 0x1D7E3 || bigger == 0x1D7ED || bigger == 0x1D7F7 || bigger == 0x1E8C7 || bigger == 0x1EE00 || bigger == 0x1EE80;
	case 0x006D: return bigger == 0x028D || bigger == 0x043C || bigger == 0x1D0D || bigger == 0x217F || bigger == 0xAB51 || bigger == 0x11700 || bigger == 0x118E3 || bigger == 0x1D426 || bigger == 0x1D45A || bigger == 0x1D48E || bigger == 0x1D4C2 || bigger == 0x1D4F6 || bigger == 0x1D52A || bigger == 0x1D55E || bigger == 0x1D592 || bigger == 0x1D5C6 || bigger == 0x1D5FA || bigger == 0x1D62E || bigger == 0x1D662 || bigger == 0x1D696;
	case 0x006E: return bigger == 0x03C0 || bigger == 0x03D6 || bigger == 0x043F || bigger == 0x0578 || bigger == 0x057C || bigger == 0x1D28 || bigger == 0x213C || bigger == 0x1D427 || bigger == 0x1D45B || bigger == 0x1D48F || bigger == 0x1D4C3 || bigger == 0x1D4F7 || bigger == 0x1D52B || bigger == 0x1D55F || bigger == 0x1D593 || bigger == 0x1D5C7 || bigger == 0x1D5FB || bigger == 0x1D62F || bigger == 0x1D663 || bigger == 0x1D697 || bigger == 0x1D6D1 || bigger == 0x1D6E1 || bigger == 0x1D70B || bigger == 0x1D71B || bigger == 0x1D745 || bigger == 0x1D755 || bigger == 0x1D77F || bigger == 0x1D78F || bigger == 0x1D7B9 || bigger == 0x1D7C9;
	case 0x006F: return bigger == 0x03BF || bigger == 0x03C3 || bigger == 0x043E || bigger == 0x0585 || bigger == 0x05E1 || bigger == 0x0647 || bigger == 0x0665 || bigger == 0x06BE || bigger == 0x06C1 || bigger == 0x06D5 || bigger == 0x06F5 || bigger == 0x0966 || bigger == 0x0A66 || bigger == 0x0AE6 || bigger == 0x0BE6 || bigger == 0x0C02 || bigger == 0x0C66 || bigger == 0x0C82 || bigger == 0x0CE6 || bigger == 0x0D02 || bigger == 0x0D66 || bigger == 0x0D82 || bigger == 0x0E50 || bigger == 0x0ED0 || bigger == 0x101D || bigger == 0x1040 || bigger == 0x10FF || bigger == 0x1D0F || bigger == 0x1D11 || bigger == 0x2134 || bigger == 0x2C9F || bigger == 0xAB3D || bigger == 0xFBA6 || bigger == 0xFBA7 || bigger == 0xFBA8 || bigger == 0xFBA9 || bigger == 0xFBAA || bigger == 0xFBAB || bigger == 0xFBAC || bigger == 0xFBAD || bigger == 0xFEE9 || bigger == 0xFEEA || bigger == 0xFEEB || bigger == 0xFEEC || bigger == 0xFF4F || bigger == 0x1042C || bigger == 0x118C8 || bigger == 0x118D7 || bigger == 0x1D428 || bigger == 0x1D45C || bigger == 0x1D490 || bigger == 0x1D4F8 || bigger == 0x1D52C || bigger == 0x1D560 || bigger == 0x1D594 || bigger == 0x1D5C8 || bigger == 0x1D5FC || bigger == 0x1D630 || bigger == 0x1D664 || bigger == 0x1D698 || bigger == 0x1D6D0 || bigger == 0x1D6D4 || bigger == 0x1D70A || bigger == 0x1D70E || bigger == 0x1D744 || bigger == 0x1D748 || bigger == 0x1D77E || bigger == 0x1D782 || bigger == 0x1D7B8 || bigger == 0x1D7BC || bigger == 0x1EE24 || bigger == 0x1EE64 || bigger == 0x1EE84;
	case 0x0070: return bigger == 0x03C1 || bigger == 0x03F1 || bigger == 0x0440 || bigger == 0x2374 || bigger == 0x2CA3 || bigger == 0xFF50 || bigger == 0x1D429 || bigger == 0x1D45D || bigger == 0x1D491 || bigger == 0x1D4C5 || bigger == 0x1D4F9 || bigger == 0x1D52D || bigger == 0x1D561 || bigger == 0x1D595 || bigger == 0x1D5C9 || bigger == 0x1D5FD || bigger == 0x1D631 || bigger == 0x1D665 || bigger == 0x1D699 || bigger == 0x1D6D2 || bigger == 0x1D6E0 || bigger == 0x1D70C || bigger == 0x1D71A || bigger == 0x1D746 || bigger == 0x1D754 || bigger == 0x1D780 || bigger == 0x1D78E || bigger == 0x1D7BA || bigger == 0x1D7C8;
	case 0x0071: return bigger == 0x051B || bigger == 0x0563 || bigger == 0x0566 || bigger == 0x1D42A || bigger == 0x1D45E || bigger == 0x1D492 || bigger == 0x1D4C6 || bigger == 0x1D4FA || bigger == 0x1D52E || bigger == 0x1D562 || bigger == 0x1D596 || bigger == 0x1D5CA || bigger == 0x1D5FE || bigger == 0x1D632 || bigger == 0x1D666 || bigger == 0x1D69A;
	case 0x0072: return bigger == 0x0433 || bigger == 0x1D26 || bigger == 0x2C85 || bigger == 0xAB47 || bigger == 0xAB48 || bigger == 0x1D42B || bigger == 0x1D45F || bigger == 0x1D493 || bigger == 0x1D4C7 || bigger == 0x1D4FB || bigger == 0x1D52F || bigger == 0x1D563 || bigger == 0x1D597 || bigger == 0x1D5CB || bigger == 0x1D5FF || bigger == 0x1D633 || bigger == 0x1D667 || bigger == 0x1D69B;
	case 0x0073: return bigger == 0x01BD || bigger == 0x0455 || bigger == 0xA731 || bigger == 0xFF53 || bigger == 0x10448 || bigger == 0x118C1 || bigger == 0x1D42C || bigger == 0x1D460 || bigger == 0x1D494 || bigger == 0x1D4C8 || bigger == 0x1D4FC || bigger == 0x1D530 || bigger == 0x1D564 || bigger == 0x1D598 || bigger == 0x1D5CC || bigger == 0x1D600 || bigger == 0x1D634 || bigger == 0x1D668 || bigger == 0x1D69C;
	case 0x0074: return bigger == 0x03C4 || bigger == 0x0442 || bigger == 0x1D1B || bigger == 0x1D42D || bigger == 0x1D461 || bigger == 0x1D495 || bigger == 0x1D4C9 || bigger == 0x1D4FD || bigger == 0x1D531 || bigger == 0x1D565 || bigger == 0x1D599 || bigger == 0x1D5CD || bigger == 0x1D601 || bigger == 0x1D635 || bigger == 0x1D669 || bigger == 0x1D69D || bigger == 0x1D6D5 || bigger == 0x1D70F || bigger == 0x1D749 || bigger == 0x1D783 || bigger == 0x1D7BD;
	case 0x0075: return bigger == 0x028B || bigger == 0x03C5 || bigger == 0x0446 || bigger == 0x057D || bigger == 0x1D1C || bigger == 0xA79F || bigger == 0xAB4E || bigger == 0xAB52 || bigger == 0x118D8 || bigger == 0x1D42E || bigger == 0x1D462 || bigger == 0x1D496 || bigger == 0x1D4CA || bigger == 0x1D4FE || bigger == 0x1D532 || bigger == 0x1D566 || bigger == 0x1D59A || bigger == 0x1D5CE || bigger == 0x1D602 || bigger == 0x1D636 || bigger == 0x1D66A || bigger == 0x1D69E || bigger == 0x1D6D6 || bigger == 0x1D710 || bigger == 0x1D74A || bigger == 0x1D784 || bigger == 0x1D7BE;
	case 0x0076: return bigger == 0x03BD || bigger == 0x0475 || bigger == 0x05D8 || bigger == 0x1D20 || bigger == 0x2174 || bigger == 0x2228 || bigger == 0x22C1 || bigger == 0xFF56 || bigger == 0x118C0 || bigger == 0x1D42F || bigger == 0x1D463 || bigger == 0x1D497 || bigger == 0x1D4CB || bigger == 0x1D4FF || bigger == 0x1D533 || bigger == 0x1D567 || bigger == 0x1D59B || bigger == 0x1D5CF || bigger == 0x1D603 || bigger == 0x1D637 || bigger == 0x1D66B || bigger == 0x1D69F || bigger == 0x1D6CE || bigger == 0x1D708 || bigger == 0x1D742 || bigger == 0x1D77C || bigger == 0x1D7B6;
	case 0x0077: return bigger == 0x026F || bigger == 0x0461 || bigger == 0x051D || bigger == 0x0561 || bigger == 0x1D21 || bigger == 0x1170E || bigger == 0x1170F || bigger == 0x1D430 || bigger == 0x1D464 || bigger == 0x1D498 || bigger == 0x1D4CC || bigger == 0x1D500 || bigger == 0x1D534 || bigger == 0x1D568 || bigger == 0x1D59C || bigger == 0x1D5D0 || bigger == 0x1D604 || bigger == 0x1D638 || bigger == 0x1D66C || bigger == 0x1D6A0;
	case 0x0078: return bigger == 0x00D7 || bigger == 0x0445 || bigger == 0x1541 || bigger == 0x157D || bigger == 0x166E || bigger == 0x2179 || bigger == 0x292B || bigger == 0x292C || bigger == 0x2A2F || bigger == 0xFF58 || bigger == 0x1D431 || bigger == 0x1D465 || bigger == 0x1D499 || bigger == 0x1D4CD || bigger == 0x1D501 || bigger == 0x1D535 || bigger == 0x1D569 || bigger == 0x1D59D || bigger == 0x1D5D1 || bigger == 0x1D605 || bigger == 0x1D639 || bigger == 0x1D66D || bigger == 0x1D6A1;
	case 0x0079: return bigger == 0x0263 || bigger == 0x028F || bigger == 0x03B3 || bigger == 0x0443 || bigger == 0x04AF || bigger == 0x10E7 || bigger == 0x1D8C || bigger == 0x1EFF || bigger == 0x213D || bigger == 0xAB5A || bigger == 0xFF59 || bigger == 0x118DC || bigger == 0x1D432 || bigger == 0x1D466 || bigger == 0x1D49A || bigger == 0x1D4CE || bigger == 0x1D502 || bigger == 0x1D536 || bigger == 0x1D56A || bigger == 0x1D59E || bigger == 0x1D5D2 || bigger == 0x1D606 || bigger == 0x1D63A || bigger == 0x1D66E || bigger == 0x1D6A2 || bigger == 0x1D6C4 || bigger == 0x1D6FE || bigger == 0x1D738 || bigger == 0x1D772 || bigger == 0x1D7AC;
	case 0x007A: return bigger == 0x1D22 || bigger == 0x118C4 || bigger == 0x1D433 || bigger == 0x1D467 || bigger == 0x1D49B || bigger == 0x1D4CF || bigger == 0x1D503 || bigger == 0x1D537 || bigger == 0x1D56B || bigger == 0x1D59F || bigger == 0x1D5D3 || bigger == 0x1D607 || bigger == 0x1D63B || bigger == 0x1D66F || bigger == 0x1D6A3;
	case 0x007B: return bigger == 0x2774 || bigger == 0x1D114;
	case 0x007D: return bigger == 0x2775;
	case 0x007E: return bigger == 0x02DC || bigger == 0x1FC0 || bigger == 0x2053 || bigger == 0x223C;
	default: return 0;
	}
}

int str_utf8_comp_names(const char *a, const char *b)
{
	int codeA;
	int codeB;
	int diff;

	while(*a && *b)
	{
		do
		{
			codeA = str_utf8_decode(&a);
		}
		while(*a && !str_utf8_isspace(codeA));

		do
		{
			codeB = str_utf8_decode(&b);
		}
		while(*b && !str_utf8_isspace(codeB));

		diff = codeA - codeB;

		if((diff < 0 && !str_utf8_is_confusable(codeA, codeB))
		|| (diff > 0 && !str_utf8_is_confusable(codeB, codeA)))
			return diff;
	}

	return *a - *b;
}

int str_utf8_isspace(int code)
{
	return code > 0x20 && code != 0xA0 && code != 0x034F && code != 0x2800 &&
		(code < 0x2000 || code > 0x200F) && (code < 0x2028 || code > 0x202F) &&
		(code < 0x205F || code > 0x2064) && (code < 0x206A || code > 0x206F) &&
		(code < 0xFE00 || code > 0xFE0F) && code != 0xFEFF &&
		(code < 0xFFF9 || code > 0xFFFC);
}

const char *str_utf8_skip_whitespaces(const char *str)
{
	const char *str_old;
	int code;

	while(*str)
	{
		str_old = str;
		code = str_utf8_decode(&str);

		// check if unicode is not empty
		if(str_utf8_isspace(code))
		{
			return str_old;
		}
	}

	return str;
}

int str_utf8_isstart(char c)
{
	if((c&0xC0) == 0x80) /* 10xxxxxx */
		return 0;
	return 1;
}

int str_utf8_rewind(const char *str, int cursor)
{
	while(cursor)
	{
		cursor--;
		if(str_utf8_isstart(*(str + cursor)))
			break;
	}
	return cursor;
}

int str_utf8_forward(const char *str, int cursor)
{
	const char *buf = str + cursor;
	if(!buf[0])
		return cursor;

	if((*buf&0x80) == 0x0)  /* 0xxxxxxx */
		return cursor+1;
	else if((*buf&0xE0) == 0xC0) /* 110xxxxx */
	{
		if(!buf[1]) return cursor+1;
		return cursor+2;
	}
	else  if((*buf & 0xF0) == 0xE0)	/* 1110xxxx */
	{
		if(!buf[1]) return cursor+1;
		if(!buf[2]) return cursor+2;
		return cursor+3;
	}
	else if((*buf & 0xF8) == 0xF0)	/* 11110xxx */
	{
		if(!buf[1]) return cursor+1;
		if(!buf[2]) return cursor+2;
		if(!buf[3]) return cursor+3;
		return cursor+4;
	}

	/* invalid */
	return cursor+1;
}

int str_utf8_encode(char *ptr, int chr)
{
	/* encode */
	if(chr <= 0x7F)
	{
		ptr[0] = (char)chr;
		return 1;
	}
	else if(chr <= 0x7FF)
	{
		ptr[0] = 0xC0|((chr>>6)&0x1F);
		ptr[1] = 0x80|(chr&0x3F);
		return 2;
	}
	else if(chr <= 0xFFFF)
	{
		ptr[0] = 0xE0|((chr>>12)&0x0F);
		ptr[1] = 0x80|((chr>>6)&0x3F);
		ptr[2] = 0x80|(chr&0x3F);
		return 3;
	}
	else if(chr <= 0x10FFFF)
	{
		ptr[0] = 0xF0|((chr>>18)&0x07);
		ptr[1] = 0x80|((chr>>12)&0x3F);
		ptr[2] = 0x80|((chr>>6)&0x3F);
		ptr[3] = 0x80|(chr&0x3F);
		return 4;
	}

	return 0;
}

static unsigned char str_byte_next(const char **ptr)
{
	unsigned char byte = **ptr;
	(*ptr)++;
	return byte;
}

static void str_byte_rewind(const char **ptr)
{
	(*ptr)--;
}

int str_utf8_decode(const char **ptr)
{
	// As per https://encoding.spec.whatwg.org/#utf-8-decoder.
	unsigned char utf8_lower_boundary = 0x80;
	unsigned char utf8_upper_boundary = 0xBF;
	int utf8_code_point = 0;
	int utf8_bytes_seen = 0;
	int utf8_bytes_needed = 0;
	while(1)
	{
		unsigned char byte = str_byte_next(ptr);
		if(utf8_bytes_needed == 0)
		{
			if(byte <= 0x7F)
			{
				return byte;
			}
			else if(0xC2 <= byte && byte <= 0xDF)
			{
				utf8_bytes_needed = 1;
				utf8_code_point = byte - 0xC0;
			}
			else if(0xE0 <= byte && byte <= 0xEF)
			{
				if(byte == 0xE0) utf8_lower_boundary = 0xA0;
				if(byte == 0xED) utf8_upper_boundary = 0x9F;
				utf8_bytes_needed = 2;
				utf8_code_point = byte - 0xE0;
			}
			else if(0xF0 <= byte && byte <= 0xF4)
			{
				if(byte == 0xF0) utf8_lower_boundary = 0x90;
				if(byte == 0xF4) utf8_upper_boundary = 0x8F;
				utf8_bytes_needed = 3;
				utf8_code_point = byte - 0xF0;
			}
			else
			{
				return -1; // Error.
			}
			utf8_code_point = utf8_code_point << (6 * utf8_bytes_needed);
			continue;
		}
		if(!(utf8_lower_boundary <= byte && byte <= utf8_upper_boundary))
		{
			// Resetting variables not necessary, will be done when
			// the function is called again.
			str_byte_rewind(ptr);
			return -1;
		}
		utf8_lower_boundary = 0x80;
		utf8_upper_boundary = 0xBF;
		utf8_bytes_seen += 1;
		utf8_code_point = utf8_code_point + ((byte - 0x80) << (6 * (utf8_bytes_needed - utf8_bytes_seen)));
		if(utf8_bytes_seen != utf8_bytes_needed)
		{
			continue;
		}
		// Resetting variables not necessary, see above.
		return utf8_code_point;
	}
}

int str_utf8_check(const char *str)
{
	int codepoint;
	while((codepoint = str_utf8_decode(&str)))
	{
		if(codepoint == -1)
		{
			return 0;
		}
	}
	return 1;
}


unsigned str_quickhash(const char *str)
{
	unsigned hash = 5381;
	for(; *str; str++)
		hash = ((hash << 5) + hash) + (*str); /* hash * 33 + c */
	return hash;
}

int pid()
{
#if defined(CONF_FAMILY_WINDOWS)
	return _getpid();
#else
	return getpid();
#endif
}

void shell_execute(const char *file)
{
#if defined(CONF_FAMILY_WINDOWS)
	ShellExecute(NULL, NULL, file, NULL, NULL, SW_SHOWDEFAULT);
#elif defined(CONF_FAMILY_UNIX)
	char* argv[2];
	argv[0] = (char*) file;
	argv[1] = NULL;
	pid_t pid = fork();
	if(!pid)
		execv(file, argv);
#endif
}

int os_compare_version(int major, int minor)
{
#if defined(CONF_FAMILY_WINDOWS)
	OSVERSIONINFO ver;
	mem_zero(&ver, sizeof(OSVERSIONINFO));
	ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&ver);
	if(ver.dwMajorVersion > major || (ver.dwMajorVersion == major && ver.dwMinorVersion > minor))
		return 1;
	else if(ver.dwMajorVersion == major && ver.dwMinorVersion == minor)
		return 0;
	else
		return -1;
#else
	return 0; // unimplemented
#endif
}

struct SECURE_RANDOM_DATA
{
	int initialized;
#if defined(CONF_FAMILY_WINDOWS)
	HCRYPTPROV provider;
#else
	IOHANDLE urandom;
#endif
};

static struct SECURE_RANDOM_DATA secure_random_data = { 0 };

int secure_random_init()
{
	if(secure_random_data.initialized)
	{
		return 0;
	}
#if defined(CONF_FAMILY_WINDOWS)
	if(CryptAcquireContext(&secure_random_data.provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
	{
		secure_random_data.initialized = 1;
		return 0;
	}
	else
	{
		return 1;
	}
#else
	secure_random_data.urandom = io_open("/dev/urandom", IOFLAG_READ);
	if(secure_random_data.urandom)
	{
		secure_random_data.initialized = 1;
		return 0;
	}
	else
	{
		return 1;
	}
#endif
}

void secure_random_fill(void *bytes, size_t length)
{
	if(!secure_random_data.initialized)
	{
		log_msg("secure", "called secure_random_fill before secure_random_init");
		dbg_break();
	}
#if defined(CONF_FAMILY_WINDOWS)
	if(!CryptGenRandom(secure_random_data.provider, length, bytes))
	{
		log_msg("secure", "CryptGenRandom failed, last_error={}", GetLastError());
		dbg_break();
	}
#else
	if(length != io_read(secure_random_data.urandom, bytes, length))
	{
		log_msg("secure", "io_read returned with a short read");
		dbg_break();
	}
#endif
}

int secure_rand()
{
	unsigned int i;
	secure_random_fill(&i, sizeof(i));
	return (int)(i%RAND_MAX);
}

#if defined(__cplusplus)
}
#endif
