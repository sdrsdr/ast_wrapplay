/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#define AST_MODULE "app_wrapplay"

/*! \file
 *
 * \brief Silly application to play an alaw sample stream based on app_mp3.c
 *
 * \author Stoian Ivanov <sdr@tera-com.com>
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 238013 $")

#include <sys/time.h>
#include <sys/types.h> 
#include <sys/wait.h> 
#include <signal.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/frame.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/app.h"

char *wrap_synopsis = "Call a wrapper to produse audo stream for a channel";
char *wrap_descrip = "  wrapPlayer(Wraper, Src):\n"
"Used to start a wrapper script/program that shall write a sample stream to it's stdout\n"
"and it will be send to the calling channel. Curenly expecting 8KHz ALaw samples\n"
"wrapper base might be something like:\n"
"  exec ffmpeg -v 0 -i $1 -y -f alaw -ar 8000 -ac 1 - 2>/dev/null\n"
"NOTE! we just call the wraper, passing src as single parameter we don't do other parameter passing (for now)\n"
//"\n"
;


static char *app = "wrapPlayer";

static int wrapplay(char *wrapper, char *filename, int fd)
{
	int res;

	res = ast_safe_fork(0);
	if (res < 0) 
		ast_log(LOG_WARNING, "Fork failed\n");
	if (res) {
		return res;
	}
	if (ast_opt_high_priority)
		ast_set_priority(0);

	dup2(fd, STDOUT_FILENO);
	ast_close_fds_above_n(STDERR_FILENO);

	/* Execute wrapper */
	execl(wrapper,  filename, (char *)NULL);
	fprintf(stderr, "Execute of %s %s failed\n",wrapper,filename);
	_exit(0);
}

static int timed_read(int fd, void *data, int datalen, int timeout)
{
	int res;
	struct pollfd fds[1];
	fds[0].fd = fd;
	fds[0].events = POLLIN;
	res = ast_poll(fds, 1, timeout);
	if (res < 1) {
		ast_log(LOG_NOTICE, "Poll timed out/errored out with %d\n", res);
		return -1;
	}
	return read(fd, data, datalen);
	
}

static int wrap_exec(struct ast_channel *chan, void *data)
{
	char *tmp;
	
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(wrapper);
		AST_APP_ARG(src);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "wrap Playback requires an arguments  (wrapper,source)\n");
		return -1;
	}
	
	tmp = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, tmp);
	
	
	int res=0;
	int fds[2];
	int ms = -1;
	int pid = -1;
	int owriteformat;
	int timeout = 2000;
	struct timeval next;
	struct ast_frame *f;
	struct myframe {
		struct ast_frame f;
		char offset[AST_FRIENDLY_OFFSET];
		short frdata[160];
	} myf = {
		.f = { 0, },
	};

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "wrap Playback requires an argument (wrapper,source)\n");
		return -1;
	}

	if (pipe(fds)) {
		ast_log(LOG_WARNING, "Unable to create pipe\n");
		return -1;
	}
	
	ast_stopstream(chan);

	owriteformat = chan->writeformat;
	res = ast_set_write_format(chan, AST_FORMAT_ALAW);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set write format to alaw\n");
		return -1;
	}
	
	
	res = wrapplay(args.wrapper, args.src, fds[1]);
	if (!strncasecmp(args.src, "http://", 7)) {
		timeout = 10000;
	}
	/* Wait 1000 ms first */
	next = ast_tvnow();
	next.tv_sec += 1;
	if (res >= 0) {
		pid = res;
		/* Order is important -- there's almost always going to be mp3...  we want to prioritize the
		   user */
		for (;;) {
			ms = ast_tvdiff_ms(next, ast_tvnow());
			if (ms <= 0) {
				res = timed_read(fds[0], myf.frdata, sizeof(myf.frdata), timeout);
				if (res > 0) {
					myf.f.frametype = AST_FRAME_VOICE;
					myf.f.subclass = AST_FORMAT_ALAW;
					myf.f.datalen = res;
					myf.f.samples = res;
					myf.f.mallocd = 0;
					myf.f.offset = AST_FRIENDLY_OFFSET;
					myf.f.src = __PRETTY_FUNCTION__;
					myf.f.delivery.tv_sec = 0;
					myf.f.delivery.tv_usec = 0;
					myf.f.data.ptr = myf.frdata;
					if (ast_write(chan, &myf.f) < 0) {
						res = -1;
						break;
					}
				} else {
					ast_debug(1, "No more samples\n");
					res = 0;
					break;
				}
				next = ast_tvadd(next, ast_samp2tv(myf.f.samples, 8000));
			} else {
				ms = ast_waitfor(chan, ms);
				if (ms < 0) {
					ast_debug(1, "Hangup detected\n");
					res = -1;
					break;
				}
				if (ms) {
					f = ast_read(chan);
					if (!f) {
						ast_debug(1, "Null frame == hangup() detected\n");
						res = -1;
						break;
					}
					if (f->frametype == AST_FRAME_DTMF) {
						ast_debug(1, "User pressed a key\n");
						ast_frfree(f);
						res = 0;
						break;
					}
					ast_frfree(f);
				} 
			}
		}
	}
	close(fds[0]);
	close(fds[1]);
	
	if (pid > -1) {
		kill(pid, SIGKILL);
	}
	
	if (!res && owriteformat)
		ast_set_write_format(chan, owriteformat);
	
	if (pid > -1) {
		waitpid (pid,NULL,0);
	}
	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application(app, wrap_exec,wrap_synopsis, wrap_descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Silly wrapPlayer Application");