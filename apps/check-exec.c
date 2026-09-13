#include "runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>

static int execute(const char* path, char* const argv[], char* const env[], int expected) {
    pid_t child = fork();
    if (child < 0) return -1;
    if (!child) {
        execve(path, argv, env);
        int error = errno;
        neva_print("D5_EXEC_ERROR: errno="); neva_print_int(error); neva_putc('\n');
        _exit(100 + error);
    }
    int status = -1;
    if (waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == expected) return 0;
    neva_print("D5_EXEC_CHILD: status="); neva_print_int(status);
    neva_print(" expected="); neva_print_int(expected); neva_putc('\n');
    return -1;
}

static int refused(const char* path, char* const args[], int expected, int kept) {
    char* env[] = { NULL };
    errno = 0;
    int result = execve(path, args, env);
    int error = errno;
    return result == -1 && error == expected && fcntl(kept, F_GETFD) >= 0 ? 0 : -1;
}

int silt_check_exec(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "d5-empty") == 0) {
        return argc == 4 && !argv[2][0] && strcmp(argv[3], "two words") == 0 ? 37 : 99;
    }
    if (argc > 1 && strcmp(argv[1], "d5-child with space") == 0) {
        if (argc != 5 || strcmp(argv[2], "/boot/d5/optional") != 0) return 90;
        char cwd[64];
        if (!getcwd(cwd, sizeof(cwd)) || strcmp(cwd, "/tmp") != 0 || umask(0077) != 0077) return 94;
        struct stat metadata = { 0 };
        int queried = stat("/tmp/d5-mask", &metadata);
        if (queried < 0 || (metadata.st_mode & 0777) != 0600) {
            neva_print("D5_EXEC_MASK: query="); neva_print_int(queried);
            neva_print(" errno="); neva_print_int(errno);
            neva_print(" mode="); neva_print_int(metadata.st_mode); neva_putc('\n');
            return 95;
        }
        if (!S_ISREG(metadata.st_mode)) return 96;
        if (chdir("/tmp/d5-mask") != -1) return 97;
        if (!getcwd(cwd, sizeof(cwd)) || strcmp(cwd, "/tmp") != 0) return 98;
        int fd = atoi(argv[3]), closed = atoi(argv[4]);
        char data[4];
        if (read(fd, data, sizeof(data)) != 4 || memcmp(data, "keep", 4) != 0) return 91;
        errno = 0;
        if (fcntl(closed, F_GETFD) != -1 || errno != EBADF) return 92;
        extern char** environ;
        for (char** entry = environ; entry && *entry; entry++) {
            if (strcmp(*entry, "D5_ENV=preserved") == 0) return 39;
        }
        return 93;
    }
    struct stat sh_info, dash_info;
    if (stat("/bin/sh", &sh_info) < 0 || stat("/bin/dash", &dash_info) < 0
        || sh_info.st_ino != dash_info.st_ino || sh_info.st_dev != dash_info.st_dev) return 16;
    char* alias_args[] = { "sh", "-c", "exit 41", NULL };
    char* alias_env[] = { "PATH=/bin", NULL };
    for (int index = 0; index < 8; index++) {
        if (execute(index & 1 ? "/bin/dash" : "/bin/sh", alias_args, alias_env, 41) < 0) return 17;
    }
    if (chdir("/tmp") < 0) return 15;
    (void)umask(0077);
    char* environment[] = { "PATH=/bin", "D5_ENV=preserved", NULL };
    char* empty[] = { "check-signals", "d5-empty", "", "two words", NULL };
    if (execute("/bin/check-signals", empty, environment, 37) < 0) return 1;
    char* boot[] = { "ignored argv zero", "", "two words", NULL };
    if (execute("/boot/check-shebang.sh", boot, environment, 23) < 0) return 2;
    int kept = open("/tmp/d5-mask", O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (kept < 0 || write(kept, "keep", 4) != 4 || lseek(kept, 0, SEEK_SET) != 0) return 3;
    int closed = fcntl(kept, F_DUPFD_CLOEXEC, 12);
    if (closed < 0) return 4;
    char kept_text[12], closed_text[12];
    snprintf(kept_text, sizeof(kept_text), "%d", kept);
    snprintf(closed_text, sizeof(closed_text), "%d", closed);
    char* child[] = { "ignored", kept_text, closed_text, NULL };
    if (execute("/boot/d5/optional", child, environment, 39) < 0) return 5;
    char* one[] = { "ignored", NULL };
    if (refused("/boot/d5/loop", one, ELOOP, kept) < 0) return 6;
    if (refused("/boot/d5/missing", one, ENOENT, kept) < 0) return 7;
    if (refused("/boot/d5/noexec", one, EACCES, kept) < 0) return 8;
    if (refused("/boot/d5/bad", one, ENOEXEC, kept) < 0) return 9;
    if (refused("/boot/d5/long", one, ENOEXEC, kept) < 0) return 10;
    char large[111]; memset(large, 'a', 110); large[110] = 0;
    char* too_large[] = { "x", large, NULL };
    if (refused("/boot/check-shebang.sh", too_large, E2BIG, kept) < 0) return 11;
    if (execute("/boot/d5/nested", one, environment, 23) < 0) return 12;
    for (int i = 0; i < 40; i++) {
        if (refused("/boot/d5/loop", one, ELOOP, kept) < 0) return 13;
    }
    if (close(closed) < 0 || close(kept) < 0) return 14;
    neva_println("D5_EXEC: empty argv/shebang/optional arg/environment/fds/refusal/recovery PASS");
    return 0;
}
