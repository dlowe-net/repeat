#include "config.h"

#include <errno.h>
#include <getopt.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

const char *REPEAT_VERSION =
    PACKAGE_STRING "\n\n"
    "Written by Daniel Lowe.\n";

const char *USAGE =
    "Repeatedly call COMMAND forever, or until specified option.\n"
    "\n"
    "Usage: %1$s [-ehipx] [--times=<n>] [--interval=<n>] <command>\n"
    "\n"
    "Options:\n"
    "  -i, --interval=DURATION  specifies the interval between invocations.\n"
    "  -t, --times=NUM          execute for number of times, then stop\n"
    "  -e, --untilerr           stop repeating when command's exit code is non-zero\n"
    "  -s, --untilsuccess       stop repeating when command's exit code is zero\n"
    "  -p, --precise   runs command at specified intervals instead of waiting\n"
    "                  the interval between executions\n"
    "  -x, --noshell   runs command via exec() instead of via \"sh -c\"\n"
    "  -h, --help      display usage and exit\n"
    "  -v, --version   display version info and exit\n"
    "\n"
    "Examples:\n"
    "  repeat echo Hello World  Prints out Hello World forever\n"
    "  repeat -n 5 echo Hello World  Prints out Hello World five times\n"
    "  repeat -i 1 echo Hello World  Prints out Hello World with a second between\n"
    "                                each invocation\n"
    "  repeat -i 1 -e -p -t 5 echo Hello World  Prints out Hello World five times,\n"
    "                                           once a second, stopping if echo\n"
    "                                           returns an error.\n"
    ;
const int32_t NS_IN_SEC = 1000000000;

int times = 0;
struct timespec interval_ts = { 0, 0 };
bool precise = false;
bool exit_on_error = false;
bool exit_on_success = false;
bool use_exec = false;
bool debug = false;
int cmd_arg_idx = 0;
char *command = NULL;

bool
parse_arguments(int argc, char *argv[], int *return_val) {
    struct option long_options[] = {
        { "times", required_argument, NULL, 't' },
        { "interval", required_argument, NULL, 'i' },
        { "precise", no_argument, NULL, 'p' },
        { "untilerr", no_argument, NULL, 'e' },
        { "untilsuccess", no_argument, NULL, 's' },
        { "noshell", no_argument, NULL, 'x' },
        { "version", no_argument, NULL, 'V' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };
    int option_idx = 0;
    char c;
    char *endp;
    float interval;

    *return_val = 0;
    opterr = 1;

    // Setting this environment variable makes getopt_long stop
    // processing options at the first non-option, which is what we
    // want for the subcommand.
    setenv("POSIXLY_CORRECT", "", false);
    while ((c = getopt_long(argc, argv, "t:i:ezdhpVx", long_options, &option_idx)) != -1) {
        switch (c) {
        case '?':
            return 1;
        case 't':
            times = strtol(optarg, &endp, 10);
            if (endp == optarg) {
                fprintf(stderr, USAGE, argv[0]);
                *return_val = 1;
                return true;
            }
            break;
        case 'd':
            printf("Debug enabled.\n");
            debug = true; break;
        case 'i':
            interval = strtof(optarg, &endp);
            if (endp == optarg) {
                fprintf(stderr, USAGE, argv[0]);
                *return_val = 1;
                return true;
            }
            if (*endp != '\0') {
                switch (*endp) {
                case 'd':
                    interval *= 86400; break;
                case 'h':
                    interval *= 3600; break;
                case 'm':
                    interval *= 60; break;
                case 's':
                    break;
                default:
                    fprintf(stderr, "Bad unit for interval - must be one of d, h, m, or s.\n");
                    *return_val = 1;
                    return true;
                }
            }
            interval_ts.tv_sec = (int)interval;
            interval_ts.tv_nsec = (interval - (int)interval) * NS_IN_SEC;
            break;
        case 'p':
            precise = true;
            break;
        case 'e':
            exit_on_error = true;
            break;
        case 's':
            exit_on_success = true;
            break;
        case 'x':
            use_exec = true;
            break;
        case 'V':
            printf("%s", REPEAT_VERSION);
            return true;
        case 'h':
            printf(USAGE, argv[0]);
            return true;
        default:
            fprintf(stderr, "Can't happen at %s:%d\n", __FILE__, __LINE__);
            *return_val = 1;
            return true;
        }
    }

    int arg_count = argc - optind;
    if (arg_count == 0) {
        fprintf(stderr, "%s\n", USAGE);
        *return_val = 1;
        return true;
    }

    cmd_arg_idx = optind;
    if (!use_exec) {
        // To use the shell, join the arguments together, separated by
        // spaces
        size_t *strlens = calloc(argc, sizeof(size_t));
        int total_len = arg_count - 1;  // start with spaces required
        for (int i = optind; i < argc; i++) {
            strlens[i] = strlen(argv[i]);
            total_len += strlens[i];
        }
        command = calloc(total_len + 1, 1);
        char *write_pt = command;
        strcpy(write_pt, argv[optind]);
        write_pt += strlens[optind];
        for (int i = optind + 1; i < argc; i++) {
            *write_pt++ = ' ';
            strcpy(write_pt, argv[i]);
            write_pt += strlens[i];
        }

        free(strlens);
    }

    return false;
}

// I bet there's a subtle overflow bug somewhere here
static inline struct timespec
timespec_add(const struct timespec * restrict a, const struct timespec * restrict b) {
    uint32_t nsecs = (a->tv_nsec % NS_IN_SEC) + (b->tv_nsec % NS_IN_SEC);
    struct timespec result = {
        a->tv_sec + b->tv_sec + (a->tv_nsec / NS_IN_SEC) + (b->tv_nsec / NS_IN_SEC) + (nsecs / NS_IN_SEC),
        nsecs % NS_IN_SEC
    };

    return result;
}

int
main(int argc, char *argv[])
{
    int exit_val = 0;
    bool exit_now = parse_arguments(argc, argv, &exit_val);
    struct timespec next_exec;

    if (exit_now) {
        return exit_val;
    }

    if (debug) {
        printf("times = %d\n", times);
        printf("interval_ts = { %ld, %ld }\n", interval_ts.tv_sec, interval_ts.tv_nsec);
        printf("precise = %s\n", (precise) ? "true":"false");
        printf("exit_on_error = %s\n", (exit_on_error) ? "true":"false");
        printf("exit_on_success = %s\n", (exit_on_success) ? "true":"false");
        printf("use_exec = %s\n", (use_exec) ? "true":"false");
        fflush(stdout);
    }

    if (precise) {
        int err = clock_gettime(CLOCK_MONOTONIC, &next_exec);
        if (err < 0) {
            fprintf(stderr, "Fatal error getting time: %s\n", strerror(errno));
            exit(1);
        }
    }

    while (true) {
        int ret;

        if (precise) {
            next_exec = timespec_add(&next_exec, &interval_ts);
        }
        if (use_exec) {
            pid_t child_pid = fork();
            if (child_pid == 0) {
                execvp(argv[cmd_arg_idx], argv + cmd_arg_idx);
                exit(1);
            }
            while (true) {
                pid_t err = wait(&ret);
                if (err == -1) {
                    if (errno != EINTR) {
                        fprintf(stderr, "Fatal error waiting on child: %s\n", strerror(errno));
                        exit(1);
                    }
                } else {
                    break;
                }
            }
        } else {
            ret = system(command);
        }

        if (ret < 0) {
            fprintf(stderr, "Couldn't run command: %s\n", strerror(errno));
            return 1;
        }
        if (WEXITSTATUS(ret) != 0 && exit_on_error) {
            return WEXITSTATUS(ret);
        }
        if (WEXITSTATUS(ret) == 0 && exit_on_success) {
            return 0;
        }

        if (WIFSIGNALED(ret) &&
            (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT)) {
            return 0;
        }

        if (times > 0) {
            times--;
            if (times == 0) {
                return WEXITSTATUS(ret);
            }
        }

        if (interval_ts.tv_sec || interval_ts.tv_nsec) {
            int err;
            if (!precise) {
                err = clock_gettime(CLOCK_MONOTONIC, &next_exec);
                if (err < 0) {
                    fprintf(stderr, "Fatal error getting time: %s\n", strerror(errno));
                    exit(1);
                }
                next_exec = timespec_add(&next_exec, &interval_ts);
            }

            do {
                err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_exec, NULL);
            } while (err != 0);
        }
    }
}
