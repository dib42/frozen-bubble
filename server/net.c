/*******************************************************************************
 *
 * Copyright (c) 2004-2012 Guillaume Cottenceau
 *
 * Portions from Mandriva's stage1.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 ******************************************************************************/

/*
 * this file holds network transmission operations.
 * it should be as far away as possible from game considerations
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <pwd.h>
#include <signal.h>
#include <regex.h>

#include <glib.h>

#include "game.h"
#include "tools.h"
#include "log.h"
#include "net.h"

/* this is set in game.c and used here for answering with
 * the requested command without passing this additional arg */
char* current_command;

const int proto_major = 1;
const int proto_minor = 2;

static char greets_msg_base[] = "SERVER_READY %s %s";
static char* servername = NULL;
static char* serverlanguage = NULL;

static char ok_generic[] = "OK";

static char fl_client_nolf[] = "NO_LF_WITHIN_TOO_MUCH_DATA (I bet you're not a regular FB client, hu?)";
static char fl_client_nulbyte[] = "NUL_BYTE_BEFORE_NEWLINE (I bet you're not a regular FB client, hu?)";
static char fl_server_full[] = "SERVER_IS_FULL";
static char fl_server_overloaded[] = "SERVER_IS_OVERLOADED";
static char fl_client_noactivity[] = "NO_ACTIVITY_WITHIN_GRACETIME";
static char fl_client_blacklisted[] = "YOU_ARE_BLACKLISTED";

static double date_amount_transmitted_reset;

#define DEFAULT_PORT 1511  // a.k.a 0xF 0xB thx misc
#define DEFAULT_MAX_USERS 255
#define DEFAULT_INTERVAL_REREGISTER 60
#define DEFAULT_MAX_TRANSMISSION_RATE 100000
#define DEFAULT_OUTPUT "INFO"
#define DEFAULT_GRACETIME 900
static int port = DEFAULT_PORT;
static int max_users = DEFAULT_MAX_USERS;
int interval_reregister = DEFAULT_INTERVAL_REREGISTER;
static int max_transmission_rate = DEFAULT_MAX_TRANSMISSION_RATE;
static int gracetime = DEFAULT_GRACETIME;

static int lan_game_mode = 0;

static int tcp_server_socket;
static int udp_server_socket = -1;

static int quiet = 0;

static char* external_hostname = "DISTANT_END";
static int external_port = -1;

static char* blacklisted_IPs = NULL;
char* pidfile = NULL;
char* user_to_switch = NULL;

static char* alert_words_file = NULL;
GList * alert_words = NULL;

static GList * conns = NULL;
static GList * conns_prio = NULL;

#define INCOMING_DATA_BUFSIZE 16384
static char * incoming_data_buffers[256];
static int incoming_data_buffers_count[256];
static time_t last_data_in[256];
static time_t minute_for_talk_flood[256];
int amount_talk_flood[256];
static int prio[256];

/* send line adding the protocol in front of the supplied msg */
static ssize_t send_line(int fd, char* msg)
{
        static char buf[16384] __attribute__((aligned(4096)));
        int size;
        if (current_command)
                size = snprintf(buf, sizeof(buf), "FB/%d.%d %s: %s\n", proto_major, proto_minor, current_command, msg);
        else
                size = snprintf(buf, sizeof(buf), "FB/%d.%d ???: %s\n", proto_major, proto_minor, msg);
        if (size > sizeof(buf)-1) {
                size = sizeof(buf)-1;
                buf[sizeof(buf)-2] = '\n';
        }
        if (size > 0) {
                return send(fd, buf, size, MSG_NOSIGNAL);
        } else {
                l2(OUTPUT_TYPE_ERROR, "[%d] Format failure, impossible to send message '%s'", fd, msg);
                return 0;
        }
}

ssize_t send_line_log(int fd, char* dest_msg, char* inco_msg)
{
        l3(OUTPUT_TYPE_DEBUG, "[%d] %s -> %s", fd, inco_msg, dest_msg);
        return send_line(fd, dest_msg);
}

ssize_t send_line_log_push(int fd, char* dest_msg)
{
        // drop pre-prio messages for connections in prio mode; there can be some remaining uninteresting
        // messages arriving right in between (TALK)
        if (prio[fd])
                return 0;
        else {
                char * tmp = current_command;
                ssize_t b;
                current_command = "PUSH";
                l2(OUTPUT_TYPE_DEBUG, "[%d] PUSH %s", fd, dest_msg);
                b = send_line(fd, dest_msg);
                current_command = tmp;
                return b;
        }
}

ssize_t send_line_log_push_binary(int fd, char* dest_msg, char* printable_msg)
{
        char * tmp = current_command;
        ssize_t b;
        current_command = "PUSH";
        l2(OUTPUT_TYPE_DEBUG, "[%d] PUSH (binary message) %s", fd, printable_msg);
        b = send_line(fd, dest_msg);
        current_command = tmp;
        return b;
}

ssize_t send_ok(int fd, char* inco_msg)
{
        return send_line_log(fd, ok_generic, inco_msg);
}


static void fill_conns_set(gpointer data, gpointer user_data)
{
        FD_SET(GPOINTER_TO_INT(data), (fd_set *) user_data);
}

static int recalculate_list_games = 0;
static GList * new_conns;
static int interrupt_loop_processing = 0;
void conn_terminated(int fd, char* reason)
{
        if (g_list_find(new_conns, GINT_TO_POINTER(fd))) {  // we can be recursively called if two players disconnect at the exact same time
                l2(OUTPUT_TYPE_CONNECT, "[%d] Closing connection: %s", fd, reason);
                close(fd);
                free(incoming_data_buffers[fd]);
                if (nick[fd] != NULL) {
                        free(nick[fd]);
                }
                if (geoloc[fd] != NULL) {
                        free(geoloc[fd]);
                }
                free(IP[fd]);
                new_conns = g_list_remove(new_conns, GINT_TO_POINTER(fd));
                player_part_game(fd);                       // this is where the recursive call can come from (process_msg_prio with a failed send)
                player_disconnects(fd);
                recalculate_list_games = 1;
                interrupt_loop_processing = 1;
                if (lan_game_mode && g_list_length(new_conns) == 0 && udp_server_socket == -1) {
                        l0(OUTPUT_TYPE_INFO, "LAN game mode server exiting on last client exit.");
                        exit(EXIT_SUCCESS);
                }
        }
}

static int prio_processed;
static time_t current_time;
static int need_another_run;
static void handle_incoming_data_generic(gpointer data, gpointer user_data, int prio)
{
        int fd = GPOINTER_TO_INT(data);

        if (!interrupt_loop_processing
            && (FD_ISSET(fd, (fd_set *) user_data)
                || (incoming_data_buffers_count[fd] > 0 && incoming_data_buffers[fd][incoming_data_buffers_count[fd]-1] == '\n'))) {
                char buf[INCOMING_DATA_BUFSIZE];
                ssize_t len;
                ssize_t offset = incoming_data_buffers_count[fd];
                incoming_data_buffers_count[fd] = 0;
                memcpy(buf, incoming_data_buffers[fd], offset);
                len = recv(fd, buf + offset, INCOMING_DATA_BUFSIZE - 1 - offset, MSG_DONTWAIT);
                if (len == -1 && errno != EAGAIN) {
                        l2(OUTPUT_TYPE_DEBUG, "[%d] System error on recv: %s", fd, strerror(errno));
                        conn_terminated(fd, "system error on recv");
                        return;

                } else if (len == 0) {
                        conn_terminated(fd, "peer shutdown");
                        return;

                } else {

                        char* ptr;
                        char* eol;

                        if (len == -1)
                                len = 0;

                        last_data_in[fd] = current_time;
                        if (minute_for_talk_flood[fd] != current_time/60) {
                                minute_for_talk_flood[fd] = current_time/60;
                                amount_talk_flood[fd] = 0;
                        }

                        len += offset;
                        // If we don't have a newline, it means we are seeing a partial send. Buffer
                        // them, since we can't synchronously wait for newline now or else we'd offer a
                        // nice easy shot for DOS (and beside, this would slow down the whole rest).
                        if (buf[len-1] != '\n') {
                                if (len == INCOMING_DATA_BUFSIZE - 1) {
                                        send_line_log_push(fd, fl_client_nolf);
                                        conn_terminated(fd, "too much data without LF");
                                        return;
                                }
                                l2(OUTPUT_TYPE_DEBUG, "[%d] buffering %zd bytes (this is normal)", fd, len);
                                memcpy(incoming_data_buffers[fd], buf, len);
                                incoming_data_buffers_count[fd] = len;
                                return;
                        }

                        /* string operations will need a NULL conn_terminated string */
                        buf[len] = '\0';
                        ptr = buf;

                        while (1) {
                                if (!(eol = strchr(ptr, '\n'))) {
                                        // the bad bad guy sent a 0 byte before the \n
                                        if (!prio)
                                                send_line_log_push(fd, fl_client_nulbyte);
                                        conn_terminated(fd, "NUL byte before newline");
                                        return;
                                }

                                if (prio) {
                                        // prio e.g. in game; we split messages because p and ! must be treated specially (see game::process_msg_prio)
                                        process_msg_prio(fd, ptr, eol - ptr + 1);
                                        len -= eol - ptr + 1;
                                        if (len == 0) {
                                                prio_processed = 1;
                                                break;
                                        }
                                        ptr = eol + 1;

                                } else {
                                        eol[0] = '\0';
                                        if (process_msg(fd, ptr)) {
                                                conn_terminated(fd, "process_msg said to shutdown this connection");
                                                return;
                                        }

                                        // process_msg > talk (flooding) > conn_terminated
                                        if (interrupt_loop_processing)
                                            return;

                                        if (eol + 1 - ptr < len) {
                                                ssize_t remaining = len - (eol + 1 - ptr);
                                                l2(OUTPUT_TYPE_DEBUG, "multiple non-prio messages for %d, buffering (%zd bytes)", fd, remaining);
                                                memcpy(incoming_data_buffers[fd], eol + 1, remaining);
                                                incoming_data_buffers_count[fd] = remaining;
                                                need_another_run = 1;
                                        }
                                        /* must handle only one message, because we might have multiple and subsequent might need to be
                                           treated as prio message if the one before is about starting the game */
                                        break;
                                }
                        }
                }
                return;
        }
        if (prio) {
                // 5 seconds is in game gracetime after which player is kicked out
                if (current_time - last_data_in[fd] > 5) {
                        conn_terminated(fd, "no activity within gracetime (during game, gracetime of 5 secs)");
                }
        } else {
                if (current_time - last_data_in[fd] > gracetime) {
                        send_line_log_push(fd, fl_client_noactivity);
                        conn_terminated(fd, "no activity within gracetime (not during game)");
                }
        }
}

static void handle_incoming_data(gpointer data, gpointer user_data)
{
        handle_incoming_data_generic(data, user_data, 0);
}
static void handle_incoming_data_prio(gpointer data, gpointer user_data)
{
        handle_incoming_data_generic(data, user_data, 1);
}

static void handle_udp_request(void)
{
        static char ok_input_beginning_base[] = "FB/%d.";
        static char * ok_input_beginning = NULL;
        static char ok_input_end[] = " SERVER PROBE";
        static char fl_unrecognized_base[] = "FB/%d.%d You don't exist, go away.\n";
        static char * fl_unrecognized = NULL;
        static char ok_answer_base[] = "FB/%d.%d SERVER HERE AT PORT %d";
        static char * ok_answer = NULL;
        int n;
        char msg[128];
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        char * answer;

        if (!ok_input_beginning)   // C sux
                ok_input_beginning = asprintf_(ok_input_beginning_base, proto_major);
        if (!ok_answer)
                ok_answer = asprintf_(ok_answer_base, proto_major, proto_minor, port);
        if (!fl_unrecognized)
                fl_unrecognized = asprintf_(fl_unrecognized_base, proto_major, proto_minor);

        memset(msg, 0, sizeof(msg));
        n = recvfrom(udp_server_socket, msg, sizeof(msg), 0, (struct sockaddr *) &client_addr, &client_len);
        if (n == -1) {
                l1(OUTPUT_TYPE_ERROR, "recvfrom: %s", strerror(errno));
                return;
        }

        l2(OUTPUT_TYPE_DEBUG, "UDP server receives %d bytes from %s.", n, inet_ntoa(client_addr.sin_addr));
        if (strncmp(msg, ok_input_beginning, strlen(ok_input_beginning)) || !strstr(msg, ok_input_end) || (lan_game_mode && g_list_length(conns_prio) > 0)) {
                answer = fl_unrecognized;
                l0(OUTPUT_TYPE_DEBUG, "Unrecognized/full.");
        } else {
                answer = ok_answer;
                l0(OUTPUT_TYPE_DEBUG, "Valid FB server probe, answering.");
        }

        if (sendto(udp_server_socket, answer, strlen(answer), 0, (struct sockaddr *) &client_addr, sizeof(client_addr)) != strlen(answer)) {
                l1(OUTPUT_TYPE_ERROR, "sendto: %s", strerror(errno));
        }
}

static char * greets_msg = NULL;
static char * get_greets_msg(void)
{
        if (!greets_msg) {
                greets_msg = asprintf_(greets_msg_base, servername, serverlanguage);
        }
        return greets_msg;
}

static char * http_get(char* host, int port, char* path);
static void download_blacklisted_IPs()
{
        char* doc = http_get("www.frozen-bubble.org", 80, "/servers/blacklisted_IPs");
        if (doc) {
                if (blacklisted_IPs)
                        free(blacklisted_IPs);
                blacklisted_IPs = asprintf_(",%s", doc);
                free(doc);
        }
}

void connections_manager(void)
{
        struct sockaddr_in client_addr;
        ssize_t len = sizeof(client_addr);
        struct timeval tv;
        double now, delta, rate;

        if (!quiet)
                download_blacklisted_IPs();
        date_amount_transmitted_reset = get_current_time_exact();

        while (1) {
                int fd;
                int retval;
                fd_set conns_set;

                reregister_server_if_needed();

                if (recalculate_list_games)
                        calculate_list_games();
                recalculate_list_games = 0;

                if (!need_another_run) {
                        FD_ZERO(&conns_set);
                        g_list_foreach(conns, fill_conns_set, &conns_set);
                        g_list_foreach(conns_prio, fill_conns_set, &conns_set);
                        if (tcp_server_socket != -1)
                                FD_SET(tcp_server_socket, &conns_set);
                        if (udp_server_socket != -1)
                                FD_SET(udp_server_socket, &conns_set);

                        tv.tv_sec = 30;
                        tv.tv_usec = 0;

                        if ((retval = select(FD_SETSIZE, &conns_set, NULL, NULL, &tv)) == -1) {
                                l1(OUTPUT_TYPE_ERROR, "select: %s", strerror(errno));
                                exit(EXIT_FAILURE);
                        }

                        // timeout
                        if (!retval)
                                continue;
                }

                current_time = get_current_time();  // a bit of caching
                prio_processed = 0;
                interrupt_loop_processing = 0;
                need_another_run = 0;
                new_conns = g_list_copy(conns_prio);
                g_list_foreach(conns_prio, handle_incoming_data_prio, &conns_set);
                g_list_free(conns_prio);
                conns_prio = new_conns;

                // prio has higher priority (astounding statement, eh?)
                if (prio_processed || interrupt_loop_processing)
                        continue;

                new_conns = g_list_copy(conns);
                g_list_foreach(conns, handle_incoming_data, &conns_set);
                g_list_free(conns);
                conns = new_conns;

                if (tcp_server_socket != -1 && FD_ISSET(tcp_server_socket, &conns_set)) {
                        if ((fd = accept(tcp_server_socket, (struct sockaddr *) &client_addr, (socklen_t *) &len)) == -1) {
                                l1(OUTPUT_TYPE_ERROR, "accept: %s", strerror(errno));
                                continue;
                        }

                        l2(OUTPUT_TYPE_CONNECT, "Accepted connection from %s: fd %d", inet_ntoa(client_addr.sin_addr), fd);
                        if (fd > 255 || conns_nb() >= max_users || (lan_game_mode && g_list_length(conns_prio) > 0)) {
                                // don't overrun prio in send_line_log_push
                                if (fd <= 255) {
                                        send_line_log_push(fd, fl_server_full);
                                }
                                l1(OUTPUT_TYPE_INFO, "[%d] Closing connection (server full)", fd);
                                close(fd);
                                continue;
                        }

                        if (blacklisted_IPs != NULL) {
                                char* blacklist_search = asprintf_(",%s", inet_ntoa(client_addr.sin_addr));
                                if (strstr(blacklisted_IPs, blacklist_search) != NULL) {
                                        send_line_log_push(fd, fl_client_blacklisted);
                                        l2(OUTPUT_TYPE_INFO, "[%d] Blacklisted client (%s)", fd, inet_ntoa(client_addr.sin_addr));
                                        close(fd);
                                        free(blacklist_search);
                                        continue;
                                }
                                free(blacklist_search);
                        }

                        now = get_current_time_exact();
                        delta = now - date_amount_transmitted_reset;
                        if (delta > 0) {
                                rate = get_reset_amount_transmitted() / delta;
                                l1(OUTPUT_TYPE_DEBUG, "Transmission rate: %.2f bytes/sec", rate);
                        } else {
                                rate = 0;
                        }
                        date_amount_transmitted_reset = now;
                        if (rate > max_transmission_rate) {
                                send_line_log_push(fd, fl_server_overloaded);
                                l1(OUTPUT_TYPE_INFO, "[%d] Closing connection (maximum transmission rate reached)", fd);
                                close(fd);
                                continue;
                        }

                        // We've really accepted this new connection. Init data.
                        last_data_in[fd] = current_time;
                        minute_for_talk_flood[fd] = current_time/60;
                        amount_talk_flood[fd] = 0;
                        nick[fd] = NULL;
                        geoloc[fd] = NULL;
                        IP[fd] = strdup_(inet_ntoa(client_addr.sin_addr));
                        prio[fd] = 0;
                        remote_proto_minor[fd] = -1;
                        send_line_log_push(fd, get_greets_msg());
                        conns = g_list_append(conns, GINT_TO_POINTER(fd));
                        player_connects(fd);
                        incoming_data_buffers[fd] = malloc_(sizeof(char) * INCOMING_DATA_BUFSIZE);
                        incoming_data_buffers_count[fd] = 0;
                        admin_authorized[fd] = streq("127.0.0.1", inet_ntoa(client_addr.sin_addr));
                        recalculate_list_games = 1;
                }

                if (udp_server_socket != -1 && FD_ISSET(udp_server_socket, &conns_set))
                        handle_udp_request();
        }
}


int conns_nb(void)
{
        return g_list_length(conns_prio) + g_list_length(conns);
}

void add_prio(int fd)
{
        conns_prio = g_list_append(conns_prio, GINT_TO_POINTER(fd));
        new_conns = g_list_remove(new_conns, GINT_TO_POINTER(fd));
        prio[fd] = 1;
        if (lan_game_mode && g_list_length(conns_prio) > 0 && udp_server_socket != -1) {
                close(tcp_server_socket);
                close(udp_server_socket);
                tcp_server_socket = udp_server_socket = -1;
        }
}

void close_server() {
        if (tcp_server_socket != -1) {
                close(tcp_server_socket);
        }
        if (udp_server_socket != -1) {
                close(udp_server_socket);
        }
        tcp_server_socket = udp_server_socket = -1;
}

static void help(void)
{
        printf("Usage: fb-server [OPTION]...\n");
        printf("\n");
        printf("     -a lang                   set the preferred language of the server (it is just an indication used by players when choosing a server, so that they can chat using their native language - you can choose none with -z)\n");
        printf("     -A alert_words_file       set the file containing alert words (one POSIX regexp by line) - this file is reread when receiving the ADMIN_REREAD command from 127.0.0.1\n");
        printf("     -c conffile               specify the path of the configuration file\n");
        printf("     -d                        debug mode: do not daemonize, and log on STDERR rather than through syslog (implies -q)\n");
        printf("     -f pidfile                set the file in which the pid of the daemon must be written\n");
        printf("     -g gracetime              set the gracetime after which a client with no network activity is terminated (in seconds, defaults to %d)\n", DEFAULT_GRACETIME);
        printf("     -h                        display this help then exits\n");
        printf("     -H host                   set the hostname (or IP) as seen from outside (by default, when registering the server to www.frozen-bubble.org, the distant end at IP level will be used)\n");
        printf("     -i minutes                set the minutes interval for reregistering on the master server (except if -q is provided); use 0 to disable the feature; defaults to %d minutes)\n", DEFAULT_INTERVAL_REREGISTER);
        printf("     -l                        LAN mode: create an UDP server (on port %d) to answer broadcasts of clients discovering where are the servers\n", DEFAULT_PORT);
        printf("     -L                        LAN/game mode: create an UDP server as above, but limit number of games to 1 (this is for an FB client hosting a LAN server)\n");
        printf("     -m max_users              set the maximum of connected users (defaults to %d, physical maximum 255 in non debug mode)\n", DEFAULT_MAX_USERS);
        printf("     -n name                   set the server name presented to players (if unset, defaults to hostname)\n");
        printf("     -o outputtype             set the output type; can be DEBUG, CONNECT, INFO, ERROR; each level includes messages of next level; defaults to INFO\n");
        printf("     -p port                   set the server port (defaults to %d)\n", DEFAULT_PORT);
        printf("     -P port                   set the server port as seen from outside (defaults to the port specified with -p)\n");
        printf("     -q                        \"quiet\" mode: don't automatically register the server to www.frozen-bubble.org\n");
        printf("     -t max_transmission_rate  set the maximum transmission rate, in bytes per second (defaults to %d)\n", DEFAULT_MAX_TRANSMISSION_RATE);
        printf("     -u user                   switch daemon to specified user\n");
        printf("     -z                        set that there is no preferred language for the server (see -a)\n");
}

static void create_udp_server(void)
{
        struct sockaddr_in server_addr;

        printf("-l: creating UDP server for answering broadcast server discover, on default port %d\n", DEFAULT_PORT);

        udp_server_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_server_socket < 0) {
                perror("socket");
                exit(EXIT_FAILURE);
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        server_addr.sin_port = htons(DEFAULT_PORT);
        if (bind(udp_server_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
                perror("bind UDP 1511");
                exit(EXIT_FAILURE);
        }
}

static void cleanup_alert_words(gpointer data, gpointer user_data)
{
        regex_t* preg = data;
        regfree(preg);
        free(preg);
}

static char* read_alert_words(char* file)
{
        char buf[512];
        FILE* f;
        if (alert_words) {
                g_list_foreach(alert_words, cleanup_alert_words, NULL);
                g_list_free(alert_words);
                alert_words = NULL;
        }
        f = fopen(file, "r");
        if (!f) {
                return asprintf_("Error opening file");
        } else {
                while (fgets(buf, sizeof(buf), f)) {
                        char errbuf[512];
                        int errcode;
                        regex_t* preg = (regex_t*) malloc_(sizeof(regex_t));
                        if (buf[strlen(buf)-1] == '\n')
                                buf[strlen(buf)-1] = '\0';
                        if ((errcode = regcomp(preg, buf, REG_EXTENDED|REG_ICASE)) != 0) {
                                regerror(errcode, preg, errbuf, sizeof(errbuf));
                                free(preg);
                                fclose(f);
                                return asprintf_("Problem compiling '%s': %s", buf, errbuf);
                        } else {
                                alert_words = g_list_append(alert_words, preg);
                        }
                }
                if (ferror(f)) {
                        fclose(f);
                        return asprintf_("Error reading file");
                }
                fclose(f);
        }
        return NULL;
}

void reread()
{
        if (alert_words_file) {
                char* response;
                l1(OUTPUT_TYPE_INFO, "Rereading alert words file '%s'.", alert_words_file);
                response = read_alert_words(alert_words_file);
                if (response) {
                        l1(OUTPUT_TYPE_ERROR, "Error rereading alert words file: '%s'\n", response);
                        free(response);
                }
        }

        l0(OUTPUT_TYPE_INFO, "Downloading latest blacklisted IPs.");
        download_blacklisted_IPs();
}

static void handle_parameter(char command, char * param) {
        char* response;
        switch (command) {
        case 'a':
                if (streq(param, "af") || streq(param, "ar") || streq(param, "az") || streq(param, "bg") || streq(param, "br")
                    || streq(param, "bs") || streq(param, "ca") || streq(param, "cs") || streq(param, "cy") || streq(param, "da")
                    || streq(param, "de") || streq(param, "el") || streq(param, "en") || streq(param, "eo") || streq(param, "eu")
                    || streq(param, "es") || streq(param, "fi") || streq(param, "fr") || streq(param, "ga") || streq(param, "gl")
                    || streq(param, "hr") || streq(param, "hu") || streq(param, "id") || streq(param, "ir") || streq(param, "is") || streq(param, "it")
                    || streq(param, "ja") || streq(param, "ko") || streq(param, "lt") || streq(param, "lv") || streq(param, "mk")
                    || streq(param, "ms") || streq(param, "nl") || streq(param, "ne") || streq(param, "no") || streq(param, "pl") || streq(param, "pt")
                    || streq(param, "pt_BR") || streq(param, "ro") || streq(param, "ru") || streq(param, "sk") || streq(param, "sl")
                    || streq(param, "sq") || streq(param, "sv") || streq(param, "tg") || streq(param, "tr") || streq(param, "uk")
                    || streq(param, "uz") || streq(param, "vi") || streq(param, "wa") || streq(param, "zh_CN") || streq(param, "zh_TW")) {
                        serverlanguage = strdup(param);
                        printf("-a: setting preferred language for users of the server to '%s'\n", serverlanguage);
                } else {
                        fprintf(stderr, "-a: '%s' not a valid language, ignoring\n", param);
                        fprintf(stderr, "    valid languages are: af, ar, az, bg, br, bs, ca, cs, cy, da, de, el, en, eo, eu, es, fi, fr, ga, gl, hr, hu, id, ir, is, it, ja, ko, lt, lv, mk, ms, nl, ne, no, pl, pt, pt_BR, ro, ru, sk, sl, sq, sv, tg, tr, uk, uz, vi, wa, zh_CN, zh_TW\n" );
                }
                break;
        case 'A':
                response = read_alert_words(param);
                if (response) {
                        fprintf(stderr, "-A: error reading alert words file '%s': %s\n", param, response);
                        free(response);
                } else {
                        printf("-A: successfully read alert words file '%s'\n", param);
                        alert_words_file = strdup(param);
                }
                break;
        case 'd':
                printf("-d: debug mode on: will not daemonize and will display log messages on STDERR\n");
                debug_mode = TRUE;
                quiet = TRUE;
                break;
        case 'f':
                printf("-f: will store pid of daemon into file '%s'\n", param);
                pidfile = strdup(param);
                break;
        case 'g':
                gracetime = charstar_to_int(param);
                if (gracetime != 0)
                        printf("-g: setting gracetime to '%d' seconds (equals '%d' minutes)\n", gracetime, gracetime/60);
                else {
                        fprintf(stderr, "-g: '%s' not convertible to int, ignoring\n", param);
                        gracetime = DEFAULT_GRACETIME;
                }
                break;
        case 'h':
                help();
                exit(EXIT_SUCCESS);
        case 'H':
                printf("-H: setting hostname as seen from outside to '%s'\n", param);
                external_hostname = strdup(param);
                break;
        case 'i':
                interval_reregister = charstar_to_int(param);
                if (interval_reregister > 0) {
                        printf("-i: setting interval for re-registering to master server to %s minutes\n", param);
                } else {
                        printf("-i: interval for re-registering to master server set to 0, e.g. disabled\n");
                }
                break;
        case 'l':
                create_udp_server();
                break;
        case 'L':
                create_udp_server();
                lan_game_mode = 1;
                break;
        case 'm':
                max_users = charstar_to_int(param);
                if (max_users > 0 && max_users <= 255)
                        printf("-m: setting maximum users to '%d'\n", max_users);
                else {
                        fprintf(stderr, "-m: '%s' not convertible to int or not in 1..255, ignoring\n", param);
                        max_users = DEFAULT_MAX_USERS;
                }
                break;
        case 'n':
                if (strlen(param) > 12) {
                        fprintf(stderr, "-n: name is too long, maximum is 12 characters\n");
                        exit(EXIT_FAILURE);
                } else {
                        int i;
                        for (i = 0; i < strlen(param); i++) {
                                if (!((param[i] >= 'a' && param[i] <= 'z')
                                      || (param[i] >= 'A' && param[i] <= 'Z')
                                      || (param[i] >= '0' && param[i] <= '9')
                                      || param[i] == '.' || param[i] == '-')) {
                                        fprintf(stderr, "-n: name must contain only chars in [a-zA-Z0-9.-]\n");
                                        exit(EXIT_FAILURE);
                                }
                        }
                        printf("-n: setting servername to '%s'\n", param);
                        servername = strdup(param);
                }
                break;
        case 'o':
                if (streq(param, "DEBUG")) {
                        printf("-o: setting output type to DEBUG\n");
                        output_type = OUTPUT_TYPE_DEBUG;
                } else if (streq(param, "CONNECT")) {
                        printf("-o: setting output type to CONNECT\n");
                        output_type = OUTPUT_TYPE_CONNECT;
                } else if (streq(param, "INFO")) {
                        printf("-o: setting output type to INFO\n");
                        output_type = OUTPUT_TYPE_INFO;
                } else if (streq(param, "ERROR")) {
                        printf("-o: setting output type to ERROR\n");
                        output_type = OUTPUT_TYPE_ERROR;
                }
                break;
        case 'p':
                port = charstar_to_int(param);
                if (port != 0) {
                        printf("-p: setting port to '%d'\n", port);
                        if (external_port == -1)
                                external_port = port;
                } else {
                        port = DEFAULT_PORT;
                        fprintf(stderr, "-p: '%s' not convertible to int, ignoring\n", param);
                }
                break;
        case 'P':
                external_port = charstar_to_int(param);
                if (external_port != 0)
                        printf("-P: setting port as seen from outside to '%d'\n", port);
                else {
                        fprintf(stderr, "-P: '%s' not convertible to int, ignoring\n", param);
                        external_port = -1;
                }
                break;
        case 'q':
                printf("-q: quiet mode: will not register to www.frozen-bubble.org\n");
                quiet = TRUE;
                break;
        case 't':
                max_transmission_rate = charstar_to_int(param);
                if (max_transmission_rate != 0)
                        printf("-t: setting maximum transmission rate to '%d' bytes/sec\n", max_transmission_rate);
                else {
                        fprintf(stderr, "-t: '%s' not convertible to int, ignoring\n", param);
                        max_transmission_rate = DEFAULT_MAX_TRANSMISSION_RATE;
                }
                break;
        case 'u':
                if (getpwnam(param) != NULL) {
                        printf("-u: will switch user of daemon to '%s'\n", param);
                        user_to_switch = strdup(param);
                } else {
                        fprintf(stderr, "-u: '%s' is not a valid user, ignoring\n", param);
                }
                break;
        case 'z':
                printf("-z: no preferred language for users of the server\n");
                serverlanguage = "zz";
                break;
        default:
                fprintf(stderr, "unrecognized option '%c', ignoring\n", command);
        }
}

void sigterm_catcher(int signum) {
        l0(OUTPUT_TYPE_INFO, "Received SIGTERM, terminating.");
        close_server();
        unregister_server();
        exit(EXIT_SUCCESS);
}

void create_server(int argc, char **argv)
{
        struct sockaddr_in client_addr;
        int valone = 1;

        while (1) {
                int c = getopt(argc, argv, "a:A:c:df:g:hH:i:lLm:n:o:p:P:qt:u:z");
                if (c == -1)
                        break;

                if (c == 'c') {
                        FILE* f;
                        printf("-c: reading configuration file %s\n", optarg);
                        f = fopen(optarg, "r");
                        if (!f) {
                                fprintf(stderr, "-c: error opening %s, ignoring\n", optarg);
                        } else {
                                char buf[8192];
                                while (fgets(buf, sizeof(buf), f)) {
                                        char command, param[256];
                                        if (buf[0] == '#' || buf[0] == '\n' || buf[0] == '\r')
                                                continue;
                                        if (sscanf(buf, "%c %255s\n", &command, param) == 2) {
                                                handle_parameter(command, param);
                                        } else if (sscanf(buf, "%c\n", &command) == 1) {
                                                handle_parameter(command, NULL);
                                        } else {
                                                fprintf(stderr, "-c: ignoring line %s\n", buf);
                                        }
                                }
                                if (ferror(f)) {
                                        fprintf(stderr, "-c: error reading %s\n", optarg);
                                }
                                fclose(f);
                        }

                } else {
                        handle_parameter(c, optarg);
                }
        }

        if (external_port == -1)
                external_port = port;

        if (!servername) {
                // Fallback to hostname
                char hostname[128];
                if (!gethostname(hostname, 127)) {
                        int i;
                        hostname[127] = '\0';  // manpage says "It is unspecified whether the truncated hostname will be null-terminated"
                        servername = g_strndup(hostname, 12);
                        for (i = 0; i < strlen(servername); i++) {
                                if (!((servername[i] >= 'a' && servername[i] <= 'z')
                                      || (servername[i] >= 'A' && servername[i] <= 'Z')
                                      || (servername[i] >= '0' && servername[i] <= '9')
                                      || servername[i] == '-')) {
                                        servername[i] = '.';
                                }
                        }
                        printf("Notice: no -n parameter, using hostname as server name presented to players: %s\n", servername);
                } else {
                        fprintf(stderr, "gethostname: %s\n", strerror(errno));
                        exit(EXIT_FAILURE);
                }
        }

        if (!serverlanguage) {
                fprintf(stderr, "Must set the preferred language of users of the server with -a <language> or specify there is none with -z.\n");
                exit(EXIT_FAILURE);
        }

        tcp_server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_server_socket < 0) {
                fprintf(stderr, "creating socket: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
        }

        setsockopt(tcp_server_socket, SOL_SOCKET, SO_REUSEADDR, &valone, sizeof(valone));

        client_addr.sin_family = AF_INET;
        client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        client_addr.sin_port = htons(port);
        if (bind(tcp_server_socket, (struct sockaddr *) &client_addr, sizeof(client_addr))) {
                fprintf(stderr, "binding port %d: %s\n", port, strerror(errno));
                exit(EXIT_FAILURE);
        }

        if (listen(tcp_server_socket, 1000) < 0) {
                fprintf(stderr, "listen: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
        }

        // Binded correctly, now we can init logging specifying the port (useful for multiple servers)
        logging_init(port);

        l2(OUTPUT_TYPE_INFO, "Created TCP game server on port %d. Servername is '%s'.", port, servername);

        signal(SIGTERM, sigterm_catcher);
}

static int mygethostbyname(char * name, struct in_addr * addr)
{
        struct hostent * h;

        h = gethostbyname(name);
        if (!h) {
                l1(OUTPUT_TYPE_DEBUG, "Unknown host %s", name);
                return -1;

        } else if (h->h_addr_list && (h->h_addr_list)[0]) {
                memcpy(addr, (h->h_addr_list)[0], sizeof(*addr));
                l2(OUTPUT_TYPE_DEBUG, "%s is at %s", name, inet_ntoa(*addr));
                return 0;
        }
        return -1;
}

static char * http_get(char * host, int port, char * path)
{
        char * buf, * ptr, * user_agent;
        char headers[4096];
        char * nextChar = headers;
        int checkedCode;
        struct in_addr serverAddress;
        struct pollfd polls;
        int sock;
        int size, bufsize, dlsize;
        int rc;
        ssize_t bytes;
        struct sockaddr_in destPort;
        char * header_content_length = "Content-Length: ";
        struct utsname uname_;

        l3(OUTPUT_TYPE_DEBUG, "HTTP_GET: retrieving http://%s:%d%s", host, port, path);

        if ((rc = mygethostbyname(host, &serverAddress))) {
                l1(OUTPUT_TYPE_ERROR, "HTTP_GET: cannot resolve %s", host);
                return NULL;
        }

        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
                l2(OUTPUT_TYPE_ERROR, "HTTP_GET: cannot create socket for connection to %s:%d", host, port);
                return NULL;
        }

        destPort.sin_family = AF_INET;
        destPort.sin_port = htons(port);
        destPort.sin_addr = serverAddress;

        if (connect(sock, (struct sockaddr *) &destPort, sizeof(destPort))) {
                close(sock);
                l2(OUTPUT_TYPE_ERROR, "HTTP_GET: cannot connect to %s:%d", host, port);
                return NULL;
        }

        uname(&uname_);
        user_agent = asprintf_("Frozen-Bubble server version %s (protocol version %d.%d) on %s/%s\n", VERSION, proto_major, proto_minor, uname_.sysname, uname_.machine);
        buf = asprintf_("GET %s HTTP/0.9\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n", path, host, user_agent);
        free(user_agent);
        if (write(sock, buf, strlen(buf)) != strlen(buf)) {
                close(sock);
                free(buf);
                l2(OUTPUT_TYPE_ERROR, "HTTP_GET: cannot write to socket for connection to %s:%d", host, port);
                return NULL;
        }
        free(buf);

        /* This is fun (well, fun for ewt); read the response a character at a time until we:

           1) Get our first \r\n; which lets us check the return code
           2) Get a \r\n\r\n, which means we're done */

        *nextChar = '\0';
        checkedCode = 0;
        while (!strstr(headers, "\r\n\r\n")) {
                polls.fd = sock;
                polls.events = POLLIN;
                rc = poll(&polls, 1, 20*1000);

                if (rc == 0) {
                        close(sock);
                        l3(OUTPUT_TYPE_ERROR, "HTTP_GET: timeout retrieving http://%s:%d%s", host, port, path);
                        return NULL;
                } else if (rc < 0) {
                        close(sock);
                        l3(OUTPUT_TYPE_ERROR, "HTTP_GET: I/O error retrieving http://%s:%d%s", host, port, path);
                        return NULL;
                }

                if (read(sock, nextChar, 1) != 1) {
                        close(sock);
                        l3(OUTPUT_TYPE_ERROR, "HTTP_GET: I/O error retrieving http://%s:%d%s", host, port, path);
                        return NULL;
                }

                nextChar++;
                *nextChar = '\0';

                if (nextChar + 1 - headers == sizeof(headers)) {
                        close(sock);
                        l3(OUTPUT_TYPE_ERROR, "HTTP_GET: I/O error retrieving http://%s:%d%s", host, port, path);
                        return NULL;
                }

                if (!checkedCode && strstr(headers, "\r\n")) {
                        char * start, * end;

                        checkedCode = 1;
                        start = headers;
                        while (!isspace(*start) && *start)
                                start++;
                        if (!*start) {
                                close(sock);
                                l3(OUTPUT_TYPE_ERROR, "HTTP_GET: I/O error retrieving http://%s:%d%s", host, port, path);
                                return NULL;
                        }
                        start++;

                        end = start;
                        while (!isspace(*end) && *end)
                                end++;
                        if (!*end) {
                                close(sock);
                                l3(OUTPUT_TYPE_ERROR, "HTTP_GET: I/O error retrieving http://%s:%d%s", host, port, path);
                                return NULL;
                        }

                        *end = '\0';
                        l1(OUTPUT_TYPE_DEBUG, "HTTP_GET: server response '%s'", start);
                        if (strcmp(start, "200")) {
                                close(sock);
                                l4(OUTPUT_TYPE_ERROR, "HTTP_GET: bad server response %s retrieving http://%s:%d%s", start, host, port, path);
                                return NULL;
                        }

                        *end = ' ';
                }
        }

        if ((buf = strstr(headers, header_content_length))) {
                size = charstar_to_int(buf + strlen(header_content_length));
                bufsize = size + 1;
        } else {
                size = -1;
                bufsize = 4096;
        }

        dlsize = 0;
        buf = ptr = malloc_(bufsize);
        while (1) {
                bytes = read(sock, ptr, bufsize - (ptr - buf) - 1);
                if (bytes == -1) {
                        l1(OUTPUT_TYPE_ERROR, "HTTP_GET: read: %s", strerror(errno));
                        close(sock);
                        free(buf);
                        return NULL;
                } else if (bytes == 0) {
                        // 0 == EOF
                        ptr[0] = '\0';
                        buf = realloc_(buf, dlsize + 1);
                        close(sock);
                        return buf;
                } else {
                        l1(OUTPUT_TYPE_DEBUG, "HTTP_GET: read %zd bytes", bytes);
                        dlsize += bytes;
                        ptr = buf + dlsize;
                        if (size > -1 && dlsize == size) {
                                ptr[0] = '\0';
                                buf = realloc_(buf, dlsize + 1);
                                close(sock);
                                return buf;
                        }
                        if (bufsize - (ptr - buf) - 1 < 2048) {
                                bufsize += 4096;
                                if (bufsize >= 1024*1024*1024) {
                                        l0(OUTPUT_TYPE_ERROR, "HTTP_GET: maximum download size of 1024*1024*1024 reached");
                                        ptr[0] = '\0';
                                        buf = realloc_(buf, dlsize + 1);
                                        close(sock);
                                        return buf;
                                }
                                buf = realloc_(buf, bufsize);
                                ptr = buf + dlsize;
                        }
                        l2(OUTPUT_TYPE_DEBUG, "HTTP_GET: dlsize %d bytes, bufsize %d bytes", dlsize, bufsize);
                }
        }
}

void register_server(int silent) {
        if (!quiet && !lan_game_mode) {
                char* path = asprintf_("/servers/servers.php?server-add=%s&server-add-port=%d", external_hostname, external_port);
                char* doc = http_get("www.frozen-bubble.org", 80, path);
                free(path);
                if (doc != NULL) {
                        if (silent) {
                                free(doc);
                                return;
                        }
                        if (strstr(doc, "FB_TAG_SERVER_ADDED")) {
                                if (streq(external_hostname, "DISTANT_END")) {
                                        // don't confuse admin printing a cryptic DISTANT_END hostname
                                        l1(OUTPUT_TYPE_INFO, "Successfully registered server (port:%d) to 'www.frozen-bubble.org'.", external_port);
                                } else {
                                        l2(OUTPUT_TYPE_INFO, "Successfully registered server (host:%s port:%d) to 'www.frozen-bubble.org'.", external_hostname, external_port);
                                }
                        } else {
                                char * ptr = doc;
                                l2(OUTPUT_TYPE_ERROR, "Problem registering server (host:%s port:%d) to 'www.frozen-bubble.org'.", external_hostname, external_port);
                                l0(OUTPUT_TYPE_ERROR, "Notice: for successful registering, using the said host and port from outside must reach this server!");
                                while ((ptr = strstr(doc, "FB_TAG_"))) {
                                        char * end = strchr(ptr, ' ');
                                        if (end) {
                                                *end = '\0';
                                                l1(OUTPUT_TYPE_ERROR, "-> %s", ptr + 7);
                                                ptr = end + 1;
                                        } else {
                                                break;
                                        }
                                }
                        }
                        free(doc);
                }
        }
}

void unregister_server() {
        if (!quiet && !lan_game_mode) {
                char* path = asprintf_("/servers/servers.php?server-remove=%s&server-remove-port=%d", external_hostname, external_port);
                char* doc = http_get("www.frozen-bubble.org", 80, path);
                free(path);
                if (doc != NULL) {
                        if (strstr(doc, "FB_TAG_SERVER_REMOVED")) {
                                if (streq(external_hostname, "DISTANT_END")) {
                                        // don't confuse admin printing a cryptic DISTANT_END hostname
                                        l1(OUTPUT_TYPE_INFO, "Successfully unregistered server (port:%d) to 'www.frozen-bubble.org'.", external_port);
                                } else {
                                        l2(OUTPUT_TYPE_INFO, "Successfully unregistered server (host:%s port:%d) to 'www.frozen-bubble.org'.", external_hostname, external_port);
                                }
                        } else {
                                char * ptr = doc;
                                l0(OUTPUT_TYPE_ERROR, "Problem unregistering server to 'www.frozen-bubble.org'.");
                                while ((ptr = strstr(doc, "FB_TAG_"))) {
                                        char * end = strchr(ptr, ' ');
                                        if (end) {
                                                *end = '\0';
                                                l1(OUTPUT_TYPE_ERROR, "-> %s", ptr + 7);
                                                ptr = end + 1;
                                        } else {
                                                break;
                                        }
                                }
                        }
                        free(doc);
                }
        }
}
