/*
 * Copyright (c) 1983, 1990, 1993, 2002
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * rlogin - remote login
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#include <sys/ioctl.h>
#ifdef HAVE_SYS_STREAM_H
#include <sys/stream.h>
#endif
#ifdef HAVE_SYS_TTY_H
#include <sys/tty.h>
#endif
#ifdef HAVE_SYS_PTYVAR_H
#include <sys/ptyvar.h>
#endif
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif

#include <netinet/in.h>
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#ifdef HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <pwd.h>
#include <setjmp.h>
#include <termios.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(HAVE_STDARG_H) && defined(__STDC__) && __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#ifdef KERBEROS
#include <kerberosIV/des.h>
#include <kerberosIV/krb.h>

CREDENTIALS cred;
Key_schedule schedule;
int use_kerberos = 1, doencrypt;
char dst_realm_buf[REALM_SZ], *dest_realm = NULL;
#endif

/*
  The TIOCPKT_* macros may not be implemented in the pty driver.
  Defining them here allows the program to be compiled.  */
#ifndef TIOCPKT
# define TIOCPKT                 _IOW('t', 112, int)
# define TIOCPKT_FLUSHWRITE      0x02
# define TIOCPKT_NOSTOP          0x10
# define TIOCPKT_DOSTOP          0x20
#endif /*TIOCPKT*/

/* The server sends us a TIOCPKT_WINDOW notification when it starts up.
   The value for this (0x80) can not overlap the kernel defined TIOCPKT_xxx
   values.  */
#ifndef TIOCPKT_WINDOW
#define	TIOCPKT_WINDOW	0x80
#endif

/* Concession to Sun.  */
#ifndef SIGUSR1
#define	SIGUSR1	30
#endif

#ifndef	_POSIX_VDISABLE
# ifdef VDISABLE
#  define _POSIX_VDISABLE VDISABLE
# else
#  define _POSIX_VDISABLE ((cc_t)'\377')
# endif
#endif

/* Returned by speed() when the specified fd is not associated with a
   terminal.  */
#define SPEED_NOTATTY	(-1)

int eight, rem;

int noescape;
u_char escapechar = '~';

#ifdef OLDSUN

struct winsize
{
  unsigned short ws_row;    /* Rows, in characters.  */
  unsigned short ws_col;    /* Columns , in characters.  */
  unsigned short ws_xpixel; /* Horizontal size, pixels.  */
  unsigned short ws_ypixel; /* Vertical size. pixels.  */
};

int   get_window_size __P ((int, struct winsize *));
#else
# define	get_window_size(fd, wp)	ioctl (fd, TIOCGWINSZ, wp)
#endif
struct	winsize winsize;

void  catch_child __P ((int));
void  copytochild __P ((int));
void  doit        __P ((sigset_t *));
void  done        __P ((int));
void  echo        __P ((char));
u_int getescape   __P ((char *));
void  lostpeer    __P ((int));
void  mode        __P ((int));
void  msg         __P ((const char *));
void  oob         __P ((int));
int   reader      __P ((sigset_t *));
void  sendwindow  __P ((void));
void  setsignal   __P ((int));
int   speed       __P ((int));
unsigned int speed_translate __P ((unsigned int));
void  sigwinch    __P ((int));
void  stop        __P ((char));
void  usage       __P ((int));
void  writer      __P ((void));
void  writeroob   __P ((int));

#ifdef	KERBEROS
void  warning    __P ((const char *, ...));
#endif

extern sig_t setsig __P ((int, sig_t));

#ifdef KERBEROS
#define	OPTIONS	"8EKde:k:l:xhV"
#else
#define	OPTIONS	"8EKde:l:hV"
#endif
static const char *short_options = OPTIONS;
static struct option long_options [] =
{
  { "debug", no_argument, 0, 'd' },
  { "user", required_argument, 0, 'l' },
  { "escape", required_argument, 0, 'e' },
  { "no-escape", no_argument, 0, 'E' },
  { "8-bit", no_argument, 0, '8' },
  { "kerberos", no_argument, 0, 'K' },
#ifdef KERBEROS
  { "realm", required_argument, 0, 'k' },
  { "encrypt", no_argument, 0, 'x' },
#endif
  { "help", no_argument, 0, 'h' },
  { "version", no_argument, 0, 'V' },
  { NULL, 0, 0, 0}
};

int
main(int argc, char *argv[])
{
  struct passwd *pw;
  struct servent *sp;
  sigset_t smask;
  uid_t uid;
  int ch, dflag;
  int term_speed;
  char *host, *user, term[1024];

#ifndef HAVE___PROGNAME
  extern char *__progname;
  __progname = argv[0];
#endif

  dflag = 0;
  host = user = NULL;

  /* Traditionnaly, if a symbolic link was made to the rlogin binary
     rlogin --> hostname
     hostname will be use as the name of the server to login too.  */
  {
    char *p = strrchr (argv[0], '/');
    if (p)
      ++p;
    else
      p = argv[0];

    if (strcmp (p, "rlogin") != 0)
      host = p;
  }

  while ((ch = getopt_long (argc, argv, short_options, long_options, NULL))
	 != EOF)
    {
      switch (ch)
	{
	  /* 8-bit input Specifying this forces us to use RAW mode input from
	     the user's terminal.  Also, in this mode we won't perform any
	     local flow control.  */
	case '8':
	  eight = 1;
	  break;

	case 'E':
	  noescape = 1;
	  break;

	case 'K':
#ifdef KERBEROS
	  use_kerberos = 0;
#endif
	  break;

	  /* Turn on the debug option for the socket.  */
	case 'd':
	  dflag = 1;
	  break;

	  /* Specify an escape character, instead of the default tilde.  */
	case 'e':
	  noescape = 0;
	  escapechar = getescape (optarg);
	  break;

#ifdef KERBEROS
	case 'k':
	  strncpy (dest_realm_buf, optarg, sizeof dest_realm_buf);
	  /* Make sure it's null termintated.  */
	  dest_realm_buf[sizeof (dest_realm_buf) - 1] = '\0';
	  dest_realm = dst_realm_buf;
	  break;
#endif

	  /* Specify the server-user-name, instead of using the name of the
	     person invoking us.  */
	case 'l':
	  user = optarg;
	  break;

#ifdef ENCRYPTION
# ifdef KERBEROS
	case 'x':
	  doencrypt = 1;
	  des_set_key (cred.session, schedule);
	  break;
# endif
#endif

	case 'V':
          printf ("rlogin (%s %s)\n", PACKAGE_NAME, PACKAGE_VERSION);
          exit (0);

	case '?':
	  usage (1);
	  break;

	case 'h':
	default:
	  usage (0);
	}
    }

  if (optind < argc)
    host = argv[optind++];

  argc -= optind;

  /* To many command line arguments or too few.  */
  if (argc > 0 || !host)
    usage (1);

  /* We must be uid root to access rcmd().  */
  if (geteuid ())
    errx (1, "must be setuid root.\n");

  /* Get the name of the user invoking us: the client-user-name.  */
  if (!(pw = getpwuid (uid = getuid ())))
    errx (1, "unknown user id.");

  /* Accept user1@host format, though "-l user2" overrides user1 */
  {
    char *p = strchr (host, '@');
    if (p)
      {
	*p = '\0';
	if (!user && p > host)
	  user = host;
	host = p + 1;
	if (*host == '\0')
	  usage (1);
      }
    if (!user)
      user = pw->pw_name;
  }

  sp = NULL;
#ifdef KERBEROS
  if (use_kerberos)
    {
      sp = getservbyname ((doencrypt ? "eklogin" : "klogin"), "tcp");
      if (sp == NULL)
	{
	  use_kerberos = 0;
	  warning ("can't get entry for %s/tcp service",
		   doencrypt ? "eklogin" : "klogin");
	}
    }
#endif

  /* Get the port number for the rlogin service.  */
  if (sp == NULL)
    sp = getservbyname ("login", "tcp");
  if (sp == NULL)
    errx (1, "login/tcp: unknown service.");

  /* Get the name of the terminal from the environment.  Also get the
     terminal's spee.  Both the name and the spee are passed to the server
     as the "cmd" argument of the rcmd() function.  This is something like
     "vt100/9600".  */
  term_speed = speed (0);
  if (term_speed == SPEED_NOTATTY)
    {
      char *p;
      snprintf (term, sizeof term, "%s",
		((p = getenv ("TERM")) ? p : "network"));
    }
  else
    {
      char *p;
      snprintf (term, sizeof term, "%s/%d",
		((p = getenv ("TERM")) ? p : "network"), term_speed);
    }
  get_window_size (0, &winsize);

  setsig (SIGPIPE, lostpeer);

  /* Block SIGURG and SIGUSR1 signals.  This will be handled by the
     parent and the child after the fork.  */
  /* Will use SIGUSR1 for window size hack, so hold it off.  */
  sigemptyset (&smask);
  sigaddset (&smask, SIGURG);
  sigaddset (&smask, SIGUSR1);
  sigprocmask (SIG_SETMASK, &smask, &smask);

  /*
   * We set SIGURG and SIGUSR1 below so that an
   * incoming signal will be held pending rather than being
   * discarded. Note that these routines will be ready to get
   * a signal by the time that they are unblocked below.
   */
  setsig (SIGURG, copytochild);
  setsig (SIGUSR1, writeroob);

#ifdef KERBEROS
 try_connect:
  if (use_kerberos)
    {
      struct hostent *hp;

      /* Fully qualify hostname (needed for krb_realmofhost).  */
      hp = gethostbyname (host);
      if (hp != NULL && !(host = strdup (hp->h_name)))
	errx (1, "%s", strerror (ENOMEM));

      rem = KSUCCESS;
      errno = 0;
      if (dest_realm == NULL)
	dest_realm = krb_realmofhost (host);

# ifdef ENCRYPTION
      if (doencrypt)
	rem = krcmd_mutual (&host, sp->s_port, user, term, 0,
			    dest_realm, &cred, schedule);
      else
#endif /* CRYPT */
	rem = krcmd (&host, sp->s_port, user, term, 0, dest_realm);
      if (rem < 0)
	{
	  use_kerberos = 0;
	  sp = getservbyname ("login", "tcp");
	  if (sp == NULL)
	    errx (1, "unknown service login/tcp.");
	  if (errno == ECONNREFUSED)
	    warning ("remote host doesn't support Kerberos");
	  if (errno == ENOENT)
	    warning ("can't provide Kerberos auth data");
	  goto try_connect;
	}
    }
  else
    {
# ifdef ENCRYPTION
      if (doencrypt)
	errx (1, "the -x flag requires Kerberos authentication.");
#endif /* CRYPT */
      rem = rcmd (&host, sp->s_port, pw->pw_name, user, term, 0);
    }
#else

  rem = rcmd (&host, sp->s_port, pw->pw_name, user, term, 0);

#endif /* KERBEROS */

  if (rem < 0)
    exit (1);

  {
    int one = 1;
    if (dflag && setsockopt (rem, SOL_SOCKET, SO_DEBUG, (char *) &one,
			     sizeof one) < 0)
      warn ("setsockopt DEBUG (ignored)");
  }

#if defined (IP_TOS) && defined (IPPROTO_IP) && defined (IPTOS_LOWDELAY)
  {
    int one = IPTOS_LOWDELAY;
    if (setsockopt (rem, IPPROTO_IP, IP_TOS, (char *)&one, sizeof (int)) < 0)
      warn ("setsockopt TOS (ignored)");
  }
#endif

  /* Now change to the real user ID.  We have to be set-user-ID root
     to get the privileged port that rcmd () uses,  however we now want to
     run as the real user who invoked us.  */
  seteuid (uid);
  setuid (uid);

  doit (&smask);

  /*NOTREACHED*/
  return 0;
}

/* Some systems, like QNX/Neutrino , The constant B0, B50,.. maps straigth to
   the actual speed, 0, 50, ..., where on other system like GNU/Linux
   it maps to a const 0, 1, ... i.e the value are encoded.
   cfgetispeed(), according to posix should return a constant value reprensenting the Baud.
   So to be portable we have to the conversion ourselves.  */
/* Some values are not not define by POSIX.  */
#ifndef B7200
#define B7200   B4800
#endif

#ifndef B14400
#define B14400  B9600
#endif

#ifndef B19200
# define B19200 B14400
#endif

#ifndef B28800
#define B28800  B19200
#endif

#ifndef B38400
# define B38400 B28800
#endif

#ifndef B57600
#define B57600  B38400
#endif

#ifndef B76800
#define B76800  B57600
#endif

#ifndef B115200
#define B115200 B76800
#endif

#ifndef B230400
#define B230400 B115200
#endif
struct termspeeds
{
  unsigned int speed;
  unsigned int sym;
} termspeeds[] =
{
  { 0,     B0 },     { 50,    B50 },   { 75,    B75 },
  { 110,   B110 },   { 134,   B134 },  { 150,   B150 },
  { 200,   B200 },   { 300,   B300 },  { 600,   B600 },
  { 1200,  B1200 },  { 1800,  B1800 }, { 2400,  B2400 },
  { 4800,   B4800 },   { 7200,  B7200 },  { 9600,   B9600 },
  { 14400,  B14400 },  { 19200, B19200 }, { 28800,  B28800 },
  { 38400,  B38400 },  { 57600, B57600 }, { 115200, B115200 },
  { 230400, B230400 }, { -1,    B230400 }
};

unsigned int
speed_translate (unsigned int sym)
{
  unsigned int i;
  for (i = 0; i < (sizeof (termspeeds) / sizeof (*termspeeds)); i++)
    {
      if (termspeeds[i].sym == sym)
	return termspeeds[i].speed;
    }
  return 0;
}

/* Returns the terminal speed for the file descriptor FD, or
   SPEED_NOTATTY if FD is not associated with a terminal.  */
int
speed (int fd)
{
  struct termios tt;

  if (tcgetattr (fd, &tt) == 0)
    {
      /* speed_t sp; */
      unsigned int sp = cfgetispeed (&tt);
      return speed_translate (sp);
    }
  return SPEED_NOTATTY;
}

pid_t child;
struct termios deftt;
struct termios nott;

void
doit (sigset_t *smask)
{
  int i;

  for (i = 0; i < NCCS; i++)
    nott.c_cc[i] = _POSIX_VDISABLE;
  tcgetattr(0, &deftt);
  nott.c_cc[VSTART] = deftt.c_cc[VSTART];
  nott.c_cc[VSTOP] = deftt.c_cc[VSTOP];

  setsig (SIGINT, SIG_IGN);
  setsignal (SIGHUP);
  setsignal (SIGQUIT);

  child = fork ();
  if (child == -1)
    {
      warn ("fork");
      done (1);
    }
  if (child == 0)
    {
      mode (1);
      if (reader (smask) == 0)
	{
	  /* If the reader () return 0, the socket to the server returned an
	     EOF, meaning the client logged out of the remote system.
	     This is the normal termination.  */
	  msg ("connection closed.");
	  exit (0);
	}
      /* If the reader () returns nonzero, the socket to the server
	 returned an error.  Somethingg went wrong.  */
      sleep (1);
      msg ("\007connection closed."); /* 007 == ASCII bell.  */
      exit (1);
    }

  /*
   * Parent process == writer.
   *
   * We may still own the socket, and may have a pending SIGURG (or might
   * receive one soon) that we really want to send to the reader.  When
   * one of these comes in, the trap copytochild simply copies such
   * signals to the child. We can now unblock SIGURG and SIGUSR1
   * that were set above.
   */
  /* Reenables SIGURG and SIUSR1.  */
  sigprocmask (SIG_SETMASK, smask, (sigset_t *) 0);

  setsig (SIGCHLD, catch_child);

  writer ();

  /* If the write returns, it means the user entered "~." on the terminal.
     In this case we terminate and the server will eventually get an EOF
     on its end of the network connection.  This should cause the server to
     log you out on the remote system.  */
  msg ("closed connection.");
  done (0);
}

/* Enable a signal handler, unless the signal is already being ignored.
   This function is called before the fork (), for SIGHUP and SIGQUIT.  */
/* Trap a signal, unless it is being ignored. */
void
setsignal (int sig)
{
  sig_t handler;
  sigset_t sigs;

  sigemptyset(&sigs);
  sigaddset(&sigs, sig);
  sigprocmask(SIG_BLOCK, &sigs, &sigs);

  handler = setsig (sig, exit);
  if (handler == SIG_IGN)
    setsig (sig, handler);

  sigprocmask(SIG_SETMASK, &sigs, (sigset_t *) 0);
}

/* This function is called by the parent:
   (1) at the end (user terminates the client end);
   (2) SIGCLD signal - the sigcld_parent () function.
   (3) SIGPPE signal - the connection has dropped.

   We send the child a SIGKILL signal, which it can't ignore, then
   wait for it to terminate.  */
void
done (int status)
{
  pid_t w;
  int wstatus;

  mode(0);
  if (child > 0)
    {
      /* make sure catch_child does not snap it up */
      setsig (SIGCHLD, SIG_DFL);
      if (kill (child, SIGKILL) >= 0)
	while ((w = wait (&wstatus)) > 0 && w != child)
	  continue;
    }
  exit (status);
}

int dosigwinch;

/*
 * This is called when the reader process gets the out-of-band (urgent)
 * request to turn on the window-changing protocol.
 */
void
writeroob (int signo)
{
  (void)signo;
  if (dosigwinch == 0)
    {
      sendwindow ();
      setsig (SIGWINCH, sigwinch);
    }
  dosigwinch = 1;
}

void
catch_child (int signo)
{
  int status;
  pid_t pid;

  (void)signo;
  for (;;)
    {
      pid = waitpid (-1, &status, WNOHANG | WUNTRACED);
      if (pid == 0)
	return;
      /* if the child (reader) dies, just quit */
      if (pid < 0 || (pid == child && !WIFSTOPPED (status)))
	done (WEXITSTATUS (status) | WTERMSIG (status));
    }
  /* NOTREACHED */
}

/*
 * writer: write to remote: 0 -> line.
 * ~.				terminate
 * ~^Z				suspend rlogin process.
 * ~<delayed-suspend char>	suspend rlogin process, but leave reader alone.
 */
void
writer ()
{
  register int bol, local, n;
  char c;

  bol = 1;			/* beginning of line */
  local = 0;
  for (;;)
    {
      n = read (STDIN_FILENO, &c, 1);
      if (n <= 0)
	{
	  if (n < 0 && errno == EINTR)
	    continue;
	  break;
	}
      /*
       * If we're at the beginning of the line and recognize a
       * command character, then we echo locally.  Otherwise,
       * characters are echo'd remotely.  If the command character
       * is doubled, this acts as a force and local echo is
       * suppressed.
       */
      if (bol)
	{
	  bol = 0;
	  if (!noescape && c == escapechar)
	    {
	      local = 1;
	      continue;
	    }
	} else if (local)
	  {
	    local = 0;
	    if (c == '.' || c == deftt.c_cc[VEOF])
	      {
		echo (c);
		break;
	      }
	    if (c == deftt.c_cc[VSUSP]
#ifdef VDSUSP
		|| c == deftt.c_cc[VDSUSP]
#endif
		)
	      {
		bol = 1;
		echo (c);
		stop (c);
		continue;
	      }
	    if (c != escapechar)
#ifdef ENCRYPTION
#ifdef KERBEROS
	      if (doencrypt)
		des_write (rem, (char *)&escapechar, 1);
	      else
#endif
#endif
		write (rem, &escapechar, 1);
	  }

#ifdef ENCRYPTION
#ifdef KERBEROS
      if (doencrypt)
	{
	  if (des_write (rem, &c, 1) == 0)
	    {
	      msg ("line gone");
	      break;
	    }
	} else
#endif
#endif
	  if (write (rem, &c, 1) == 0)
	    {
	      msg ("line gone");
	      break;
	    }
      bol = c == deftt.c_cc[VKILL] || c == deftt.c_cc[VEOF] ||
	c == deftt.c_cc[VINTR] || c == deftt.c_cc[VSUSP] ||
	c == '\r' || c == '\n';
    }
}

void
echo (register char c)
{
  register char *p;
  char buf[8];

  p = buf;
  c &= 0177;
  *p++ = escapechar;
  if (c < ' ')
    {
      *p++ = '^';
      *p++ = c + '@';
    }
  else if (c == 0177)
    {
      *p++ = '^';
      *p++ = '?';
    }
  else
    *p++ = c;
  *p++ = '\r';
  *p++ = '\n';
  write (STDOUT_FILENO, buf, p - buf);
}

void
stop (char cmdc)
{
  mode (0);
  setsig (SIGCHLD, SIG_IGN);
  kill (cmdc == deftt.c_cc[VSUSP] ? 0 : getpid (), SIGTSTP);
  setsig (SIGCHLD, catch_child);
  mode (1);
  sigwinch (0);			/* check for size changes */
}

void
sigwinch (int signo)
{
  struct winsize ws;

  (void)signo;
  if (dosigwinch && get_window_size(0, &ws) == 0
      && memcmp(&ws, &winsize, sizeof ws))
    {
      winsize = ws;
      sendwindow ();
    }
}

/*
 * Send the window size to the server via the magic escape
 */
void
sendwindow ()
{
  struct winsize *wp;
  char obuf[4 + sizeof (struct winsize)];

  wp = (struct winsize *)(obuf+4);
  obuf[0] = 0377;
  obuf[1] = 0377;
  obuf[2] = 's';
  obuf[3] = 's';
  wp->ws_row = htons (winsize.ws_row);
  wp->ws_col = htons (winsize.ws_col);
  wp->ws_xpixel = htons (winsize.ws_xpixel);
  wp->ws_ypixel = htons (winsize.ws_ypixel);

#ifdef ENCRYPTION
#ifdef KERBEROS
  if(doencrypt)
    des_write (rem, obuf, sizeof obuf);
  else
#endif
#endif
    write (rem, obuf, sizeof obuf);
}

/*
 * reader: read from remote: line -> 1
 */
#define	READING	1
#define	WRITING	2

jmp_buf rcvtop;
pid_t ppid;
int rcvcnt, rcvstate;
char rcvbuf[8 * 1024];

void
oob (int signo)
{
  struct termios tt;
  int atmark, n, out, rcvd;
  char waste[BUFSIZ], mark;

  (void)signo;
  out = O_RDWR;
  rcvd = 0;
  while (recv (rem, &mark, 1, MSG_OOB) < 0)
    {
      switch (errno)
	{
	case EWOULDBLOCK:
	  /*
	   * Urgent data not here yet.  It may not be possible
	   * to send it yet if we are blocked for output and
	   * our input buffer is full.
	   */
	  if ((size_t)rcvcnt < sizeof rcvbuf)
	    {
	      n = read (rem, rcvbuf + rcvcnt, sizeof(rcvbuf) - rcvcnt);
	      if (n <= 0)
		return;
	      rcvd += n;
	    }
	  else
	    {
	      n = read (rem, waste, sizeof waste);
	      if (n <= 0)
		return;
	    }
	  continue;
	default:
	  return;
	}
    }
  if (mark & TIOCPKT_WINDOW)
    {
      /* Let server know about window size changes */
      kill (ppid, SIGUSR1);
    }
  if (!eight && (mark & TIOCPKT_NOSTOP))
    {
      tcgetattr (0, &tt);
      tt.c_iflag &= ~(IXON | IXOFF);
      tt.c_cc[VSTOP] = _POSIX_VDISABLE;
      tt.c_cc[VSTART] = _POSIX_VDISABLE;
      tcsetattr (0, TCSANOW, &tt);
    }
  if (!eight && (mark & TIOCPKT_DOSTOP))
    {
      tcgetattr(0, &tt);
      tt.c_iflag |= (IXON|IXOFF);
      tt.c_cc[VSTOP] = deftt.c_cc[VSTOP];
      tt.c_cc[VSTART] = deftt.c_cc[VSTART];
      tcsetattr (0, TCSANOW, &tt);
    }
  if (mark & TIOCPKT_FLUSHWRITE)
    {
#ifdef TIOCFLUSH
      ioctl (1, TIOCFLUSH, (char *)&out);
#endif
      for (;;)
	{
	  if (ioctl (rem, SIOCATMARK, &atmark) < 0)
	    {
	      warn ("ioctl SIOCATMARK (ignored)");
	      break;
	    }
	  if (atmark)
	    break;
	  n = read (rem, waste, sizeof waste);
	  if (n <= 0)
	    break;
	}
      /*
       * Don't want any pending data to be output, so clear the recv
       * buffer.  If we were hanging on a write when interrupted,
       * don't want it to restart.  If we were reading, restart
       * anyway.
       */
      rcvcnt = 0;
      longjmp (rcvtop, 1);
    }

  /* oob does not do FLUSHREAD (alas!) */

  /*
   * If we filled the receive buffer while a read was pending, longjmp
   * to the top to restart appropriately.  Don't abort a pending write,
   * however, or we won't know how much was written.
   */
  if (rcvd && rcvstate == READING)
    longjmp (rcvtop, 1);
}

/* reader: read from remote: line -> 1 */
int
reader (sigset_t *smask)
{
  pid_t pid;
  int n, remaining;
  char *bufp;

#if BSD >= 43 || defined(SUNOS4)
  pid = getpid ();		/* modern systems use positives for pid */
#else
  pid = -getpid ();	/* old broken systems use negatives */
#endif

  setsig (SIGTTOU, SIG_IGN);
  setsig (SIGURG, oob);

  ppid = getppid ();
  fcntl (rem, F_SETOWN, pid);
  setjmp (rcvtop);
  sigprocmask (SIG_SETMASK, smask, (sigset_t *) 0);
  bufp = rcvbuf;
  for (;;)
    {
      while ((remaining = rcvcnt - (bufp - rcvbuf)) > 0)
	{
	  rcvstate = WRITING;
	  n = write (STDOUT_FILENO, bufp, remaining);
	  if (n < 0)
	    {
	      if (errno != EINTR)
		return -1;
	      continue;
	    }
	  bufp += n;
	}
      bufp = rcvbuf;
      rcvcnt = 0;
      rcvstate = READING;

#ifdef ENCRYPTION
#ifdef KERBEROS
      if (doencrypt)
	rcvcnt = des_read (rem, rcvbuf, sizeof rcvbuf);
      else
#endif
#endif
	rcvcnt = read (rem, rcvbuf, sizeof rcvbuf);
      if (rcvcnt == 0)
	return 0;
      if (rcvcnt < 0)
	{
	  if (errno == EINTR)
	    continue;
	  warn ("read");
	  return -1;
	}
    }
}

void
mode (int f)
{
  struct termios tt;

  switch (f)
    {
    case 0:
      tcsetattr (0, TCSADRAIN, &deftt);
      break;
    case 1:
      tt = deftt;
      tt.c_oflag &= ~(OPOST);
      tt.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
      tt.c_iflag &= ~(ICRNL);
      tt.c_cc[VMIN] = 1;
      tt.c_cc[VTIME] = 0;
      if (eight)
	{
	  tt.c_iflag &= ~(IXON | IXOFF | ISTRIP);
	  tt.c_cc[VSTOP] = _POSIX_VDISABLE;
	  tt.c_cc[VSTART] = _POSIX_VDISABLE;
	}
      tcsetattr(0, TCSADRAIN, &tt);
      break;

    default:
      return;
    }
}

void
lostpeer (int signo)
{
  (void)signo;
  setsig (SIGPIPE, SIG_IGN);
  msg ("\007connection closed.");
  done (1);
}

/* copy SIGURGs to the child process. */
void
copytochild (int signo)
{
  (void)signo;
  kill (child, SIGURG);
}

void
msg (const char *str)
{
  fprintf (stderr, "rlogin: %s\r\n", str);
}

#ifdef KERBEROS
/* VARARGS */
void
#if defined(HAVE_STDARG_H) && defined(__STDC__) && __STDC__
warning (const char *fmt, ...)
#else
warning (fmt, va_alist)
     char *fmt;
     va_dcl
#endif
{
  va_list ap;

  fprintf (stderr, "rlogin: warning, using standard rlogin: ");
#if defined(HAVE_STDARG_H) && defined(__STDC__) && __STDC__
  va_start (ap, fmt);
#else
  va_start (ap);
#endif
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fprintf (stderr, ".\n");
}
#endif

void
usage (int status)
{
  if (status)
    {
      fprintf (stderr,
	       "Usage: rlogin [ -%s]%s[-e char] [ -l username ] [username@]host\n",
#ifdef KERBEROS
# ifdef ENCRYPTION
	       "8EKx", " [-k realm] "
# else
	       "8EK", " [-k realm] "
# endif
#else
	       "8EL", " "
#endif
	       );
      fprintf (stderr, "Try rlogin --help for more information.\n");
    }
  else
    {
      puts ("Usage: rlogin [OPTION] ... hostname");
      puts ("Rlogin starts a terminal session on a remote host host.");
      puts ("\
  -8, --8-bit       allows an eight-bit input data path at all times");
      puts ("\
  -E, --no-escape   stops any character from being recognized as an escape\n\
                    character");
      puts ("\
  -d, --debug       turns on socket debugging (see setsockopt(2))");
      puts ("\
  -e, --escape=CHAR allows user specification of the escape character,\n\
                    which is ``~'' by default");
  puts ("\
  -l, --user USER   run as USER on the remote system");
#ifdef KERBEROS
      puts ("\
  -K, --kerberos    turns off all Kerberos authentication");
      puts ("\
  -k, --realm=REALM requests rlogin to obtain tickets for the remote host in\n\
                    REALM realm instead of the remote host's realm");
#ifdef ENCRYPTION
      puts ("\
  -x, --encrypt     turns on DES encryption for all data passed via the\n\
                    rlogin session");
#endif
#endif
      puts ("\
  -V, --version     display program version");
      puts ("\
  -h, --help        display usage instructions");
      fprintf (stdout, "\nSubmit bug reports to %s.\n", PACKAGE_BUGREPORT);
    }
  exit (status);
}

/*
 * The following routine provides compatibility (such as it is) between older
 * Suns and others.  Suns have only a `ttysize', so we convert it to a winsize.
 */
#ifdef OLDSUN
int
get_window_size (int fd, struct winsize *wp)
{
  struct ttysize ts;
  int error;

  if ((error = ioctl (0, TIOCGSIZE, &ts)) != 0)
    return error;
  wp->ws_row = ts.ts_lines;
  wp->ws_col = ts.ts_cols;
  wp->ws_xpixel = 0;
  wp->ws_ypixel = 0;
  return 0;
}
#endif

u_int
getescape (register char *p)
{
  long val;
  int len;

  if ((len = strlen (p)) == 1)	/* use any single char, including '\'.  */
    return ((u_int)*p);
  /* otherwise, \nnn */
  if (*p == '\\' && len >= 2 && len <= 4)
    {
      val = strtol (++p, NULL, 8);
      for (;;)
	{
	  if (!*++p)
	    return ((u_int)val);
	  if (*p < '0' || *p > '8')
	    break;
	}
    }
  msg ("illegal option value -- e");
  usage (1);
  /* NOTREACHED */
  return 0;
}
