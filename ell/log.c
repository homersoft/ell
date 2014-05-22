/*
 *
 *  Embedded Linux library
 *
 *  Copyright (C) 2011-2014  Intel Corporation. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fnmatch.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "util.h"
#include "log.h"
#include "private.h"

/**
 * SECTION:log
 * @short_description: Logging framework
 *
 * Logging framework
 */

/**
 * l_debug_desc:
 *
 * Debug descriptor.
 */

static void log_null(int priority, const char *file, const char *line,
			const char *func, const char *format, va_list ap)
{
}

static l_log_func_t log_func = log_null;
static const char *log_ident = "";
static int log_fd = -1;
static unsigned long log_pid;

static inline void close_log(void)
{
	if (log_fd > 0) {
		close(log_fd);
		log_fd = -1;
	}
}

static int open_log(const char *path)
{
	struct sockaddr_un addr;

	log_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (log_fd < 0)
		return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path));

	if (connect(log_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		close_log();
		return -1;
	}

	return 0;
}

/**
 * l_log_set_ident:
 * @ident: string identifier
 *
 * Sets the log identifier string.
 **/
LIB_EXPORT void l_log_set_ident(const char *ident)
{
	log_ident = ident;
}

/**
 * l_log_set_handler:
 * @function: log handler function
 *
 * Sets the log handler function.
 **/
LIB_EXPORT void l_log_set_handler(l_log_func_t function)
{
	L_DEBUG_SYMBOL(__debug_intern, "");

	close_log();

	if (!function) {
		log_func = log_null;
		return;
	}

	log_func = function;
}

/**
 * l_log_set_null:
 *
 * Disable logging.
 **/
LIB_EXPORT void l_log_set_null(void)
{
	close_log();

	log_func = log_null;
}

static void log_stderr(int priority, const char *file, const char *line,
			const char *func, const char *format, va_list ap)
{
	vfprintf(stderr, format, ap);
}

/**
 * l_log_set_stderr:
 *
 * Enable logging to stderr.
 **/
LIB_EXPORT void l_log_set_stderr(void)
{
	close_log();

	log_func = log_stderr;
}

static void log_syslog(int priority, const char *file, const char *line,
			const char *func, const char *format, va_list ap)
{
	struct msghdr msg;
	struct iovec iov[2];
	char hdr[64], *str;
	int hdr_len, str_len;

	str_len = vasprintf(&str, format, ap);
	if (str_len < 0)
		return;

	hdr_len = snprintf(hdr, sizeof(hdr), "<%i>%s[%lu]: ", priority,
					log_ident, (unsigned long) log_pid);

	iov[0].iov_base = hdr;
	iov[0].iov_len  = hdr_len;
	iov[1].iov_base = str;
	iov[1].iov_len  = str_len;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = iov;
	msg.msg_iovlen = 2;

	sendmsg(log_fd, &msg, 0);

	free(str);
}

/**
 * l_log_set_syslog:
 *
 * Enable logging to syslog.
 **/
LIB_EXPORT void l_log_set_syslog(void)
{
	close_log();

	if (open_log("/dev/log") < 0) {
		log_func = log_null;
		return;
	}

	log_pid = getpid();

	log_func = log_syslog;
}

static void log_journal(int priority, const char *file, const char *line,
			const char *func, const char *format, va_list ap)
{
	struct msghdr msg;
	struct iovec iov[12];
	char prio[16], *str;
	int prio_len, str_len;

	str_len = vasprintf(&str, format, ap);
	if (str_len < 0)
		return;

	prio_len = snprintf(prio, sizeof(prio), "PRIORITY=%u\n", priority);

	iov[0].iov_base = "MESSAGE=";
	iov[0].iov_len  = 8;
	iov[1].iov_base = str;
	iov[1].iov_len  = str_len - 1;
	iov[2].iov_base = prio;
	iov[2].iov_len  = prio_len;
	iov[3].iov_base = "CODE_FILE=";
	iov[3].iov_len  = 10;
	iov[4].iov_base = (char *) file;
	iov[4].iov_len  = strlen(file);
	iov[5].iov_base = "\n";
	iov[5].iov_len  = 1;
	iov[6].iov_base = "CODE_LINE=";
	iov[6].iov_len  = 10;
	iov[7].iov_base = (char *) line;
	iov[7].iov_len  = strlen(line);
	iov[8].iov_base = "\n";
	iov[8].iov_len  = 1;
	iov[9].iov_base = "CODE_FUNC=";
	iov[9].iov_len  = 10;
	iov[10].iov_base = (char *) func;
	iov[10].iov_len  = strlen(func);
	iov[11].iov_base = "\n";
	iov[11].iov_len  = 1;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = iov;
	msg.msg_iovlen = 12;

	sendmsg(log_fd, &msg, 0);

	free(str);
}

/**
 * l_log_set_journal:
 *
 * Enable logging to journal.
 **/
LIB_EXPORT void l_log_set_journal(void)
{
	close_log();

	if (open_log("/run/systemd/journal/socket") < 0) {
		log_func = log_null;
		return;
	}

	log_pid = getpid();

	log_func = log_journal;
}

/**
 * l_log_with_location:
 * @priority: priority level
 * @file: source file
 * @line: source line
 * @func: source function
 * @format: format string
 * @...: format arguments
 *
 * Log information.
 **/
LIB_EXPORT void l_log_with_location(int priority,
				const char *file, const char *line,
				const char *func, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	log_func(priority, file, line, func, format, ap);
	va_end(ap);
}

/**
 * l_error:
 * @format: format string
 * @...: format arguments
 *
 **/

/**
 * l_warn:
 * @format: format string
 * @...: format arguments
 *
 **/

/**
 * l_info:
 * @format: format string
 * @...: format arguments
 *
 **/

/**
 * l_debug:
 * @format: format string
 * @...: format arguments
 **/

static const char *debug_pattern;

void debug_enable(struct l_debug_desc *start, struct l_debug_desc *stop)
{
	struct l_debug_desc *desc;
	char *pattern_copy;

	if (!debug_pattern)
		return;

	pattern_copy = strdupa(debug_pattern);

	while (pattern_copy) {
		char *str = strsep(&pattern_copy, ":,");
		if (!str)
			break;

		for (desc = start; desc < stop; desc++) {
			if (!fnmatch(str, desc->file, 0))
				desc->flags |= L_DEBUG_FLAG_PRINT;
			if (!fnmatch(str, desc->func, 0))
				desc->flags |= L_DEBUG_FLAG_PRINT;
		}
	}
}

extern struct l_debug_desc __start___debug[];
extern struct l_debug_desc __stop___debug[];

/**
 * l_debug_enable:
 * @pattern: debug pattern
 *
 * Enable debug sections based on @pattern.
 **/
LIB_EXPORT void l_debug_enable(const char *pattern)
{
	if (!pattern)
		return;

	debug_pattern = pattern;

	debug_enable(__start___debug, __stop___debug);
}

/**
 * l_debug_disable:
 *
 * Disable all debug sections.
 **/
LIB_EXPORT void l_debug_disable(void)
{
	struct l_debug_desc *desc;

	for (desc = __start___debug; desc < __stop___debug; desc++)
		desc->flags &= ~L_DEBUG_FLAG_PRINT;

	debug_pattern = NULL;
}
