/* MicroDLNA project
 *
 * http://sourceforge.net/projects/microdlna/
 *
 * This file is part of MicroDLNA.
 *
 * Copyright (c) 2016, Gabor Simon
 * All rights reserved.
 *
 * With alternations by 
 * Michael J.Walsh
 * Copyright (c) 2025
 *
 * Based on the MiniDLNA project:
 * Copyright (C) 2008-2012  Justin Maggard
 *
 * MicroDLNA is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MicroDLNA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MicroDLNA. If not, see <http://www.gnu.org/licenses/>.
 *
 * Portions of the code from the MiniUPnP project:
 *
 * Copyright (c) 2006-2007, Thomas Bernard
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * The name of the author may not be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include "globalvars.h"
#include "getifaddr.h"
#include "log.h"
#include "minissdp.h"
#include "threads.h"
#include "upnpdescgen.h"
#include "upnpevents.h"
#include "upnphttp.h"
#include "utils.h"
#include "version.h"

// global vars
int listening_port = 2800;      /* HTTP Port */
int notify_interval = 895;      /* seconds between SSDP announces */
int max_connections = 10;       /* max number of simultaneous conenctions */
int mode_systemd = 0;           /* systemd-compatible mode or not */

char *media_dir = NULL;

#define FRIENDLYNAME_MAX_LEN 64
char friendly_name[FRIENDLYNAME_MAX_LEN] = { '\0' };

#define UUID_LEN 42
char uuidvalue[UUID_LEN] = { '\0' };

// var local to this file
static uid_t uid = (uid_t)-1;
static volatile int quitting = 0;
static int foreground_execution = 0;
static int shttpl = -1;
static int sssdp = -1;
static int log_fd = -1;
static char *pidfilename = NULL;

static struct option long_options[] = {
    // general options
    { "help", no_argument, NULL, 'h' },
    { "version", no_argument, NULL, 'V' },
    { "debug", no_argument, NULL, 'd' },
    { "verbose", no_argument, NULL, 'v' },
    { "mode-systemd", no_argument, NULL, 'S' },
    { "foreground", no_argument, NULL, 'g' },
    { "config-file", required_argument, NULL, 'f' },

    // media settings
    { "media-dir", required_argument, NULL, 'D' },

    // running environment
    { "user", required_argument, NULL, 'u' },
    { "log-file", required_argument, NULL, 'L' },
    { "log-level", required_argument, NULL, 'l' },
    { "pid-file", required_argument, NULL, 'P' },

    // network config
    { "port", required_argument, NULL, 'p' },
    { "network-interface", required_argument, NULL, 'i' },
    { "max-connections", required_argument, NULL, 'c' },

    // UPnP settings
    { "notify-interval", required_argument, NULL, 't' },
    { "uuid", required_argument, NULL, 'U' },
    { "friendly-name", required_argument, NULL, 'F' },
    { NULL, 0, NULL, 0 }
};

static void readoptionsfile(const char *optionsfile, const char *arg0);

/* OpenAndConfHTTPSocket() :
 * setup the socket used to handle incoming HTTP connections. */
static int open_and_conf_http_socket(int port)
{
    int s = socket(PF_INET, SOCK_STREAM, 0);

    if (s < 0)
    {
        PRINT_LOG(E_ERROR, "socket(http): %d\n", errno);
        return -1;
    }

    int i = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) < 0)
        PRINT_LOG(E_ERROR, "setsockopt(http, SO_REUSEADDR): %d\n", errno);

    struct sockaddr_in listenname;
    memset(&listenname, 0, sizeof(struct sockaddr_in));
    listenname.sin_family = AF_INET;
    listenname.sin_port = htons(port);
    listenname.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr *)&listenname, sizeof(struct sockaddr_in)) < 0)
    {
        PRINT_LOG(E_ERROR, "bind(http): %d\n", errno);
        close(s);
        return -1;
    }

    if (listen(s, 6) < 0)
    {
        PRINT_LOG(E_ERROR, "listen(http): %d\n", errno);
        close(s);
        return -1;
    }

    return s;
}

/* Handler for the SIGTERM signal (kill)
 * SIGINT is also handled */
static void sigterm(int sig)
{
    signal(sig, SIG_IGN);       /* Ignore this signal while we are quitting */
    PRINT_LOG(E_DEBUG, "received signal %d, good-bye\n", sig);
    quitting = 1;
}

static void sighup(int sig)
{
    signal(sig, sighup);
    PRINT_LOG(E_DEBUG, "received signal %d, re-read\n", sig);
    reload_ifaces(1, sssdp);
}

static void help(const char *arg0)
{
    printf("Usage: %s options\n", arg0);
    printf("General options:\n");
    printf("    -h, --help\n");
    printf("        Display this help\n");
    printf("    -V, --version\n");
    printf("        Print the version number\n");
    printf("    -f, --config-file <path>\n");
    printf("        Read the specified options file\n");

    printf("Media settings:\n");
    printf("    -D, --media-dir <path>\n");
    printf("        Media dir to publish, MANDATORY\n");

    printf("Running environment:\n");
    printf("    -u, --user <uid or username>\n");
    printf("        User name or uid to use, now: ");

    const struct passwd *entry = getpwuid(uid);
    if (entry)
        puts(entry->pw_name);
    else
        printf("%u\n", uid);

    printf("    -L, --log-file <path>\n");
    printf("        The path of the log file\n");
    printf("    -l, --log-level <n>\n");
    printf("        Log level can be: off, error, info or debug\n");
    printf("    -P, --pid-file <path>\n");
    printf("        Name of the pid file\n");
    printf("    -d, --debug\n");
    printf("        Debug mode (will not daemonize)\n");
    printf("    -v, --verbose\n");
    printf("        Enable verbose messages\n");
    printf("    -S, --mode-systemd\n");
    printf("        Systemd-compatible mode\n");
    printf("    -g, --foreground\n");
    printf("        Foreground execution\n");

    printf("Network config:\n");
    printf("    -p, --port <n>\n");
    printf("        Port for HTTP traffic, now: %d\n", listening_port);
    printf("    -i, --network-interface <comma-separated list>\n");
    printf("        Interfaces to listen on, default: all\n");
    printf("    -c, --max-connections <n>\n");
    printf("        Maximal number of concurrent connections, now: %d\n",
           max_connections);

    printf("UPnP settings:\n");
    printf("    -t, --notify-interval <n>\n");
    printf("        Notification broadcast interval, now: %d\n", notify_interval);
    printf("    -U, --uuid <string>\n");
    printf("        UUID to use, now: %s\n", uuidvalue + 5);
    printf("    -F, --friendly-name <string>\n");
    printf("        Friendly name, now: %s\n", friendly_name);
}

// this function can receive args from the command line and the
// config file
static void process_option(int c, const char *arg_value, const char *arg_name,
                           const char *arg0)
{
    switch (c)
    {
    case 'i':                  // --network_interface
        set_interfaces_from_string(arg_value);
        break;

    case 'p':                  // --port
        listening_port = atoi(arg_value);
        if (listening_port < 1 || listening_port > 65535)
            EXIT_ERROR("Invalid port %s.\n", arg_value);
        break;

    case 't':                  // --notify_interval
        notify_interval = atoi(arg_value);
        if (notify_interval < 1)
            EXIT_ERROR("Invalid notify interval %s.\n", arg_value);
        break;

    case 'U':                  // --uuid
        if (!validate_uuid(arg_value))
            EXIT_ERROR("Invalid uuid '%s'.\n", arg_value);

        strcpy(uuidvalue, "uuid:");
        strxcpy(uuidvalue + 5, arg_value, UUID_LEN - 5);
        break;

    case 'F':                  // --friendly_name
        strxcpy(friendly_name, arg_value, FRIENDLYNAME_MAX_LEN);
        break;

    case 'D':                  // --media_dir
        if (media_dir != NULL)
            free(media_dir);

        media_dir = safe_strdup(arg_value);
        break;

    case 'L':                  // --log_file
        log_fd = open(arg_value, O_WRONLY | O_APPEND | O_CREAT, 0666);
        if (log_fd < 0)
            EXIT_ERROR("Failed to open logfile '%s': %d\n", arg_value, errno);
        break;

    case 'l':                  // --log_level
        set_debug_level(arg_value);
        break;

    case 'u':                  // --user
    {
        char *string;
        uid = strtoul(arg_value, &string, 0);
        if (*string != '\0')
        {
            /* Symbolic username given, not UID. */
            const struct passwd *entry = getpwnam(arg_value);
            if (!entry)
                EXIT_ERROR("Bad user '%s'.\n", arg_value);
            uid = entry->pw_uid;
        }
    }
    break;

    case 'c':                  // --max_connections
        max_connections = atoi(arg_value);
        if (max_connections < 1)
            EXIT_ERROR("Invalid max connections '%s'.\n", arg_value);
        break;

    case 'P':                  // --pid_file
        if (pidfilename != NULL)
            free(pidfilename);

        pidfilename = safe_strdup(arg_value);
        break;

    case 'd':                  // --debug
        log_level = E_DEBUG;
        foreground_execution = 1;
        break;

    case 'v':                  // --verbose
        log_level = E_ERROR;
        break;

    case 'f':                  // --options_file
        // load options file
        readoptionsfile(arg_value, arg0);
        break;

    case 'h':                  // --help
        help(arg0);
        exit(0);

    case 'S':                  // --mode_systemd
        mode_systemd = 1;
        foreground_execution = 1;
        break;

    case 'g':                  // --foreground
        foreground_execution = 1;
        break;

    case 'V':                  // --version
        print_version();
        exit(0);

    case '?':                  // invalid option
        EXIT_ERROR("Unknown option: %s\n", arg_name);

    case ':':                  // missing mandatory argument for an option
        EXIT_ERROR("Missing argument for option: %c\n", optopt);
    }
}

static inline int is_not_eol(const char p)
{
    return p != '\0' && p != '\r' && p != '\n' && p != '#';
}

static inline struct option *find_option(const char *name)
{
    for (int i = 0; long_options[i].name; i++)
        if (strcmp(name, long_options[i].name) == 0)
            return &long_options[i];

    return NULL;
}

static void readoptionsfile(const char *optionsfile, const char *arg0)
{
    char line_buffer[1024];

    FILE *f = fopen(optionsfile, "r");

    if (!f)
        EXIT_ERROR("Error opening options file '%s': %d\n", optionsfile, errno);

    while (1)
    {
        char *start = fgets(&line_buffer[0], 1024, f);
        if (!start)
        {
            if (!feof(f))
                EXIT_ERROR("Error reading options file '%s': %d\n", optionsfile, errno);
            break;
        }

        // skip leading whitespaces
        while (*start == ' ' || *start == '\t')
            start++;

        const char *name = start;

        // find first '=' char
        char *p;
        for (p = start; is_not_eol(*p) && *p != '='; ++p)
        {
            if (*p == '_')
                *p = '-';
        }

        if (*p != '=')
            continue;
        *p = '\0';
        const char *value = ++p;

        // either trim the string or cut off a trailing comment
        for (; is_not_eol(*p); ++p)
            ;
        *p = '\0';

        // process this arg
        const struct option *opt = find_option(name);
        if (!opt)
            EXIT_ERROR("Unknown option: %s\n", name);

        if (opt->has_arg == no_argument)
            EXIT_ERROR("Invalid config file option: %s\n", name);

        process_option(opt->val, value, opt->name, arg0);
    }
    fclose(f);
}

static void sig_error(const char *sig_type) __attribute__((noreturn));
static void sig_error(const char *sig_type)
{
    EXIT_ERROR("Failed to set %s handler. EXITING.\n", sig_type);
}

static void set_signal_handlers(void)
{
    /* set signal handlers */
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction));

    sa.sa_handler = sigterm;
    if (sigaction(SIGTERM, &sa, NULL))
        sig_error("SIGTERM");
    if (sigaction(SIGINT, &sa, NULL))
        sig_error("SIGINT");
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        sig_error("SIGPIPE");
    if (signal(SIGHUP, &sighup) == SIG_ERR)
        sig_error("SIGHUP");
}

static void logged_dup2(int fd, int fno, const char *name)
{
    if (dup2(fd, fno) == -1)
        PRINT_LOG(E_ERROR, "Failed to redirect %s: %d\n", name, errno);
}

static void init_friendly_name(void)
{
    if (friendly_name[0] == '\0')
    {
        if (gethostname(friendly_name, FRIENDLYNAME_MAX_LEN) == 0)
        {
            friendly_name[FRIENDLYNAME_MAX_LEN - 1] = '\0';
            char *p = strchr(friendly_name, '.');
            if (p)
                *p = '\0';
        }
        else
            strcpy(friendly_name, "Unknown");
    }
}

static void init(int argc, char *const *argv)
{
    // process the command line
    int c;

    while ((c =
                getopt_long(argc, argv, ":hVdvSgf:D:u:L:l:P:p:i:c:t:U:F:",
                            long_options, NULL)) != -1)
    {
        process_option(c, optarg, argv[optind - 1], argv[0]);
    }

    // set defaults for those option not set on command line or in options file
    set_uuid_value();

    // give a name to this server
    init_friendly_name();

    if (!media_dir)
    {
        fprintf(stderr, "Error: You must specify a media dir\n");
        fprintf(stderr, "Usage: %s -D [media dir]\n", argv[0]);
        exit(1);
    }

    log_short_version();

    sssdp = open_ssdp_receive_socket();
    if (sssdp < 0)
        EXIT_ERROR("Failed to open socket for receiving SSDP. EXITING\n");

    /* open socket for HTTP connections. */
    shttpl = open_and_conf_http_socket(listening_port);
    if (shttpl < 0)
        EXIT_ERROR("Failed to open socket for HTTP. EXITING\n");

    PRINT_LOG(E_INFO, "HTTP listening on port %d\n", listening_port);

    // logging
    if (foreground_execution)
    {
        if (log_fd > -1)
            close(log_fd);

        log_fd = -1;
        free(pidfilename);
        pidfilename = NULL;
    }
    else if (log_fd == -1)
    {
        log_level = E_OFF;
        log_fd = open("/dev/null", O_WRONLY, 0666);
        if (log_fd < 0)
            EXIT_ERROR("Failed to open /dev/null, quitting: %d\n", errno);
    }

    // open pidfile
    FILE *pid_fh = NULL;
    if (pidfilename)
    {
        pid_fh = fopen(pidfilename, "w");
        if (!pid_fh)
            EXIT_ERROR("Failed to open pidfile '%s': %d\n", pidfilename, errno);

        if (uid != -1 && fchown(fileno(pid_fh), uid, -1) != 0)
            EXIT_ERROR("Unable to change pidfile %s ownership: %d\n", pidfilename, errno);

        // we need an absolute path since we will change directories later
        pidfilename = realpath(pidfilename, NULL);
    }

    // fork
    if (!foreground_execution)
    {
        int pid = fork();
        if (pid == -1)
        {
            EXIT_ERROR("Fork failed: %d\n", errno);
        }
        else if (pid > 0)
        {
            if (pid_fh)
            {
                if (fprintf(pid_fh, "%d\n", pid) <= 0)
                {
                    kill(pid, SIGTERM);
                    EXIT_ERROR("Unable to write to pidfile %s: %d\n", pidfilename, errno);
                }

                fclose(pid_fh);
                free(pidfilename);
                pidfilename = NULL;
            }
            exit(0);
        }

        /* obtain a new process group */
        if (setsid() < 0)
            EXIT_ERROR("setsid failed: %d\n", errno);
    }

    // init threads
    init_threads();

    // signals
    set_signal_handlers();

    // lower user permissions
    if (uid != -1 && setuid(uid) == -1)
        EXIT_ERROR("Failed to switch to uid '%d'. [%d] EXITING.\n", uid, errno);

    // redirect all further output to the log
    if (log_fd > -1)
    {
        logged_dup2(log_fd, STDOUT_FILENO, "stdout");
        logged_dup2(log_fd, STDERR_FILENO, "stderr");
    }
}

int main(int argc, char *const *argv)
{
    // initialise the system
    init(argc, argv);

    reload_ifaces(0, sssdp);

    struct timeval timeout, timeofday, lastnotifytime = { 0, 0 };
    lastnotifytime.tv_sec = time(NULL) + notify_interval;

    /* main loop */
    while (!quitting)
    {
        /* Check if we need to send SSDP NOTIFY messages and do it if
         * needed */
        if (gettimeofday(&timeofday, 0) < 0)
        {
            PRINT_LOG(E_ERROR, "gettimeofday(): %d\n", errno);
            timeout.tv_sec = notify_interval;
            timeout.tv_usec = 0;
        }
        /* the comparison is not very precise but who cares ? */
        else if (timeofday.tv_sec >= (lastnotifytime.tv_sec + notify_interval))
        {
            PRINT_LOG(E_DEBUG, "Sending SSDP notifies\n");
            send_all_ssdp_notifies();
            memcpy(&lastnotifytime, &timeofday, sizeof(struct timeval));
            timeout.tv_sec = notify_interval;
            timeout.tv_usec = 0;
        }
        else
        {
            timeout.tv_sec = lastnotifytime.tv_sec + notify_interval - timeofday.tv_sec;
            if (timeofday.tv_usec > lastnotifytime.tv_usec)
            {
                timeout.tv_usec = 1000000 + lastnotifytime.tv_usec - timeofday.tv_usec;
                timeout.tv_sec--;
            }
            else
                timeout.tv_usec = lastnotifytime.tv_usec - timeofday.tv_usec;
        }

        /* select open sockets (SSDP, HTTP listen, and all HTTP soap sockets) */
        fd_set readset;
        FD_ZERO(&readset);

        int max_fd = -1;
        if (sssdp >= 0)
        {
            FD_SET(sssdp, &readset);
            max_fd = MAX(max_fd, sssdp);
        }

        if (shttpl >= 0)
        {
            FD_SET(shttpl, &readset);
            max_fd = MAX(max_fd, shttpl);
        }

        fd_set writeset;
        FD_ZERO(&writeset);
        upnpevents_selectfds(&readset, &writeset, &max_fd);

        int ret = select(max_fd + 1, &readset, &writeset, 0, &timeout);
        if (ret < 0)
        {
            if (quitting)
                goto shutdown;
            if (errno == EINTR)
                continue;
            PRINT_LOG(E_ERROR, "select(all): %d\n", errno);
            EXIT_ERROR("Failed to select open sockets. EXITING\n");
        }
        upnpevents_processfds(&readset, &writeset);
        upnpevents_removed_timedout_subs();

        /* process SSDP packets */
        if (sssdp >= 0 && FD_ISSET(sssdp, &readset))
            process_ssdp_request(sssdp);

        /* process incoming HTTP connections */
        if (shttpl >= 0 && FD_ISSET(shttpl, &readset))
        {
            struct sockaddr_in clientname;
            socklen_t clientnamelen = sizeof(struct sockaddr_in);
            int shttp = accept(shttpl, (struct sockaddr *)&clientname, &clientnamelen);

            if (shttp < 0)
            {
                PRINT_LOG(E_ERROR, "accept(http): %d\n", errno);
                continue;
            }

            // reject connections from unknown interfaces
            int iface = -1;

            if (clientname.sin_addr.s_addr != INADDR_LOOPBACK)
            {
                iface = get_interface(&clientname.sin_addr);
                if (iface == -1)
                {
                    close(shttp);
                    PRINT_LOG(E_DEBUG, "Rejected HTTP connection from %s:%d\n",
                              inet_ntoa_ts(clientname.sin_addr),
                              ntohs(clientname.sin_port));
                    continue;
                }
            }

            PRINT_LOG(E_DEBUG, "Accepted HTTP connection from %s:%d\n",
                      inet_ntoa_ts(clientname.sin_addr), ntohs(clientname.sin_port));

            // process a http connection
            if (!process_upnphttp_http_query(shttp, iface))
            {
                PRINT_LOG(E_ERROR, "process_upnphttp_http_query() failed\n");
                close(shttp);
            }
        }
    }

shutdown:

    send_all_ssdp_goodbyes();
    clear_upnpevent_subscribers();
    upnpevents_clear_notify_list();
    free_ifaces();

    if (sssdp >= 0)
        close(sssdp);
    if (shttpl >= 0)
        close(shttpl);

    if (pidfilename && unlink(pidfilename) < 0)
        PRINT_LOG(E_ERROR, "Failed to remove pidfile %s: %d\n", pidfilename, errno);

    free(media_dir);
    free(pidfilename);

    PRINT_LOG(E_INFO, "exiting program\n");

    exit(EXIT_SUCCESS);
}
