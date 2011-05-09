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

ASTERISK_FILE_VERSION(__FILE__, "0.1")

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


#define ALAW_SAMPLEFORMAT AST_FORMAT_ALAW
#define ALAW_SAMPLESPERSECOND 8000
#define ALAW_SAMPLESINFRAME 160
#define ALAW_BYPESPERSAMPLE 1

#define SLIN_SAMPLEFORMAT AST_FORMAT_SLINEAR
#define SLIN_SAMPLESPERSECOND 8000
#define SLIN_SAMPLESINFRAME 160
#define SLIN_BYPESPERSAMPLE 2



static char *app = "wrapPlayer";
static char *wrap_synopsis = "Call a wrapper to produse audo stream for a channel (ALAW format)";
static char *wrap_descrip = "  wrapPlayer(Wraper, Src):\n"
"Used to start a wrapper script/program that shall write a sample stream to it's stdout\n"
"and it will be send to the calling channel. Curenly expecting 8KHz ALaw samples\n"
"wrapper base might be something like:\n"
"  exec ffmpeg -v 0 -i $1 -y -f alaw -ar 8000 -ac 1 - 2>/dev/null\n"
"NOTE! we just call the wraper, passing src as single parameter we don't do other parameter passing (for now)\n"
//"\n"
;


static char *apps = "wrapPlayerS";
static char *wrap_synopsiss = "Call a wrapper to produse audo stream for a channel (SLINEAR format)";
static char *wrap_descrips = "  wrapPlayerS(Wraper, Src):\n"
"Used to start a wrapper script/program that shall write a sample stream to it's stdout\n"
"and it will be send to the calling channel. Curenly expecting 8KHz signed 2 bytes samples (SLINEAR)\n"
"wrapper base might be something like:\n"
"  exec ffmpeg -v 0 -i $1 -y -f s16le -ar 8000 -ac 1 - 2>/dev/null\n"
"NOTE! we just call the wraper, passing src as single parameter we don't do other parameter passing (for now)\n"
//"\n"
;



static int wrapplay(char *wrapper, char *src, int fd)
{
	ast_verbose ("wrapPlayer(S?) will execute  %s %s \n",wrapper,src);
	int res;
//	int x;
	sigset_t fullset, oldset;
/*	
#ifdef HAVE_CAP
	cap_t cap;
#endif
*/
	sigfillset(&fullset);
	pthread_sigmask(SIG_BLOCK, &fullset, &oldset);

	res = fork();
	if (res < 0) 
		ast_log(LOG_WARNING, "Fork failed\n");
	if (res) {
		pthread_sigmask(SIG_SETMASK, &oldset, NULL);
		return res;
	}
/*
#ifdef HAVE_CAP
	cap = cap_from_text("cap_net_admin-eip");

	if (cap_set_proc(cap)) {
		ast_log(LOG_WARNING, "Unable to remove capabilities.\n");
	}
	cap_free(cap);
#endif
*/	
	
	if (ast_opt_high_priority)
		ast_set_priority(0);
	signal(SIGPIPE, SIG_DFL);
	pthread_sigmask(SIG_UNBLOCK, &fullset, NULL);

	dup2(fd, STDOUT_FILENO);
	//for (x=STDERR_FILENO + 1;x<256;x++) {
	//	close(x);
	//}

	/* Execute wrapper */
	execl(wrapper, wrapper, src, (char *)NULL);
	fprintf(stderr, "Execute of %s %s failed\n",wrapper,src);
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

struct myframe_base {
	struct ast_frame f;
	char frdata[];
};

struct myframe_alaw {
	struct myframe_base b;
	char data[AST_FRIENDLY_OFFSET+(2+ALAW_SAMPLESINFRAME)*ALAW_BYPESPERSAMPLE];
};

struct myframe_slin {
	struct myframe_base b;
	char data[AST_FRIENDLY_OFFSET+(2+SLIN_SAMPLESINFRAME)*SLIN_BYPESPERSAMPLE];
};


static int wrap_exec_real(struct ast_channel *chan, void *data, struct myframe_base * myf, int sampleformat, int samplespersecond, int bypespersample,int samplesinframe);
static int wrap_exec(struct ast_channel *chan, void *data) {
	struct myframe_alaw myf;
	return wrap_exec_real(chan,data,&myf.b,ALAW_SAMPLEFORMAT,ALAW_SAMPLESPERSECOND,ALAW_BYPESPERSAMPLE,ALAW_SAMPLESINFRAME);
}
static int wrap_execs(struct ast_channel *chan, void *data) {
	struct myframe_slin myf;
	return wrap_exec_real(chan,data,&myf.b,SLIN_SAMPLEFORMAT,SLIN_SAMPLESPERSECOND,SLIN_BYPESPERSAMPLE,SLIN_SAMPLESINFRAME);
}

static int wrap_exec_real(struct ast_channel *chan, void *data, struct myframe_base * myf, int sampleformat, int samplespersecond, int bypespersample,int samplesinframe)
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
	int waitms = 500;
	int msrem = -1;
	int pid = -1;
	int owriteformat;
	int timeout = 2000;
	
	
	//frame to read channel events in
	struct ast_frame *f;
	

	
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
	if (owriteformat!=sampleformat) {
		res = ast_set_write_format(chan, sampleformat);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set suitable write format (%x)\n",sampleformat);
			return -1;
		}
	} else {
		ast_verbose ("Chanel format is already suitble (%x)\n",sampleformat);
	}
	
	
	res = wrapplay(args.wrapper, args.src, fds[1]);
	if (!strncasecmp(args.src, "http://", 7)) {
		timeout = 5000;
	}
	if (!strncasecmp(args.src, "rtsp://", 7)) {
		timeout = 5000;
	}
	int seq=0;
	long ts=0;
	if (res >= 0) {
		pid = res;
		/* Order is important -- there's almost always going to be mp3...  we want to prioritize the
		   user */
		for (;;) {
			if (waitms <= 0) {
				res = timed_read(fds[0], myf->frdata+AST_FRIENDLY_OFFSET, samplesinframe*bypespersample, timeout);
				if (res > 0) {
					myf->f.frametype = AST_FRAME_VOICE;
					myf->f.subclass = sampleformat;
					myf->f.samples = res/bypespersample;
					myf->f.mallocd = 0;
					myf->f.src = __PRETTY_FUNCTION__;
					myf->f.delivery.tv_sec = 0;
					myf->f.delivery.tv_usec = 0;
					waitms=(myf->f.samples * 1000)/samplespersecond;
					myf->f.len=waitms;
					myf->f.seqno=seq; seq++;
					myf->f.ts=ts; ts+=waitms;
					
					if (waitms>15) waitms-=7;
					AST_FRAME_SET_BUFFER(&(myf->f),&(myf->frdata),AST_FRIENDLY_OFFSET,res);
					if (ast_write(chan, &(myf->f)) < 0) {
						res = -1;
						break;
					}
				} else {
					ast_debug(1, "No more samples\n");
					res = 0;
					break;
				}
			} else {
				msrem = ast_waitfor(chan, waitms);
				if (msrem < 0) {
					ast_debug(1, "Hangup detected\n");
					res = -1;
					break;
				}
				if (msrem) {
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
				waitms=msrem;
			}
		}
	}
	close(fds[0]);
	close(fds[1]);
	
	if (pid > -1) {
		kill(pid, SIGKILL);
	}
	
	if (!res && owriteformat!=chan->writeformat)
		ast_verbose ("Resetting chanle write format to old write format (%x)\n",owriteformat);
		ast_set_write_format(chan, owriteformat);
	
	if (pid > -1) {
		waitpid (pid,NULL,0);
	}
	return res;
}


static int unload_module(void)
{
	return ast_unregister_application(apps) || ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application(apps, wrap_execs,wrap_synopsiss, wrap_descrips) || ast_register_application(app, wrap_exec,wrap_synopsis, wrap_descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Silly wrapPlayer/wrapPalayerS Applications for ALaw/SLINEAR formats");
