#pragma once

#include <stdint.h>
#include <sys/_sigset.h>
#include <sys/types.h>

#if !defined(_SIGSET_T_DECLARED)
#define _SIGSET_T_DECLARED
typedef __sigset_t sigset_t;
#endif

// Signal values are the Neva userspace ABI. Values without a specialized
// kernel action still have the default terminate behavior.
#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGSEGV 11
#define SIGSYS 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGUSR1 16
#define SIGUSR2 17
#define SIGCHLD 18
#define SIGCONT 19
#define SIGSTOP 20
#define SIGTSTP 21
#define SIGTTIN 22
#define SIGTTOU 23
#define NSIG 24

typedef int sig_atomic_t;
typedef void (*sighandler_t)(int signal_number);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

struct sigaction {
    sighandler_t sa_handler;
    sigset_t sa_mask;
    int sa_flags;
};

sighandler_t signal(int signal_number, sighandler_t handler);
int sigaction(int signal_number, const struct sigaction* action,
              struct sigaction* previous);
int sigemptyset(sigset_t* set);
int sigfillset(sigset_t* set);
int sigaddset(sigset_t* set, int signal_number);
int sigdelset(sigset_t* set, int signal_number);
int sigismember(const sigset_t* set, int signal_number);
int sigprocmask(int operation, const sigset_t* set, sigset_t* previous);
int sigsuspend(const sigset_t* mask);
int kill(pid_t process, int signal_number);
int killpg(pid_t process_group, int signal_number);
int raise(int signal_number);
