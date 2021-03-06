/* ISC license. */

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/sgetopt.h>
#include <skalibs/strerr2.h>
#include <skalibs/uint.h>
#include <skalibs/tai.h>
#include <skalibs/iopause.h>
#include <skalibs/djbunix.h>
#include <s6/config.h>

#define USAGE "s6-setlock [ -r | -w ] [ -n | -N | -t timeout ] lockfile prog..."
#define dieusage() strerr_dieusage(100, USAGE)

typedef int lockfunc_t (int) ;
typedef lockfunc_t *lockfunc_t_ref ;

static lockfunc_t_ref f[2][2] = { { &lock_sh, &lock_shnb }, { &lock_ex, &lock_exnb } } ;

int main (int argc, char const *const *argv, char const *const *envp)
{
  unsigned int nb = 0, ex = 1 ;
  unsigned int timeout = 0 ;
  PROG = "s6-setlock" ;
  for (;;)
  {
    register int opt = subgetopt(argc, argv, "nNrwt:") ;
    if (opt == -1) break ;
    switch (opt)
    {
      case 'n' : nb = 1 ; break ;
      case 'N' : nb = 0 ; break ;
      case 'r' : ex = 0 ; break ;
      case 'w' : ex = 1 ; break ;
      case 't' : if (!uint0_scan(subgetopt_here.arg, &timeout)) dieusage() ;
                 nb = 2 ; break ;
      default : dieusage() ;
    }
  }
  argc -= subgetopt_here.ind ; argv += subgetopt_here.ind ;
  if (argc < 2) dieusage() ;

  if (nb < 2)
  {
    int fd = open_create(argv[0]) ;
    if (fd == -1) strerr_diefu2sys(111, "open_create ", argv[0]) ;
    if ((*f[ex][nb])(fd) == -1) strerr_diefu2sys(1, "lock ", argv[0]) ;
  }
  else
  {
    char const *cargv[3] = { "s6lockd-helper", argv[0], 0 } ;
    char const *cenvp[2] = { ex ? "S6LOCK_EX=1" : 0, 0 } ;
    iopause_fd x = { .events = IOPAUSE_READ } ;
    tain_t deadline ;
    int p[2] ;
    unsigned int pid ;
    char c ;
    tain_now_g() ;
    tain_from_millisecs(&deadline, timeout) ;
    tain_add_g(&deadline, &deadline) ;
    pid = child_spawn(S6_LIBEXECPREFIX "s6lockd-helper", cargv, cenvp, p, 2) ;
    if (!pid) strerr_diefu2sys(111, "spawn ", S6_LIBEXECPREFIX "s6lockd-helper") ;
    x.fd = p[0] ;
    for (;;)
    {
      register int r = iopause_g(&x, 1, &deadline) ;
      if (r < 0) strerr_diefu1sys(111, "iopause") ;
      if (!r)
      {
        kill(pid, SIGTERM) ;
        errno = ETIMEDOUT ;
        strerr_diefu1sys(1, "acquire lock") ;
      }
      r = sanitize_read(fd_read(p[0], &c, 1)) ;
      if (r < 0) strerr_diefu1sys(111, "read ack from helper") ;
      if (r) break ;
    }
    if (c != '!') strerr_dief1x(111, "helper sent garbage ack") ;
    fd_close(p[0]) ;
    if (uncoe(p[1]) < 0) strerr_diefu1sys(111, "uncoe fd to helper") ;
  }
  pathexec_run(argv[1], argv+1, envp) ;
  strerr_dieexec(111, argv[1]) ;
}
