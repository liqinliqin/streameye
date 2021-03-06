
/*
 * Copyright (c) Calin Crisan
 * This file is part of streamEye.
 *
 * streamEye is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "common.h"
#include "streameye.h"


    /* locals */

static int client_read_timeout = DEF_CLIENT_READ_TIMEOUT;
static int tcp_port = 0;
static int listen_localhost = 0;
static char *input_separator = NULL;
static client_t **clients = NULL;
static int num_clients = 0;


    /* globals */

int log_level = 1; /* 0 - quiet, 1 - info, 2 - debug */
char jpeg_buf[JPEG_BUF_LEN];
int jpeg_size = 0;
int running = 1;
pthread_cond_t jpeg_cond;
pthread_mutex_t jpeg_mutex;
pthread_mutex_t clients_mutex;


    /* local functions */

static int          init_server();
static client_t   * wait_for_client(int socket_fd);
static void         print_help();


    /* server socket */

int init_server() {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        ERRNO("socket() failed");
        return -1;
    }

    int tr = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &tr, sizeof(tr)) < 0) {
        ERRNO("setsockopt() failed");
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    if (listen_localhost) {
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    }
    else {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    }
    server_addr.sin_port = htons(tcp_port);

    if (bind(socket_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        ERRNO("bind() failed");
        close(socket_fd);
        return -1;
    }

    if (listen(socket_fd, 5) < 0) {
        ERRNO("listen() failed");
        close(socket_fd);
        return -1;
    }

    if (fcntl(socket_fd, F_SETFL, O_NONBLOCK) < 0) {
        ERRNO("fcntl() failed");
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}

client_t *wait_for_client(int socket_fd) {
    struct sockaddr_in client_addr;
    unsigned int client_len = sizeof(client_addr);

    /* wait for a connection */
    int stream_fd = accept(socket_fd, (struct sockaddr *) &client_addr, &client_len);
    if (stream_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ERRNO("accept() failed");
        }

        return NULL;
    }

    /* set socket timeout */
    struct timeval tv;

    tv.tv_sec = client_read_timeout;
    tv.tv_usec = 0;

    setsockopt(stream_fd, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv, sizeof(struct timeval));

    /* create client structure */
    client_t *client = malloc(sizeof(client_t));
    memset(client, 0, sizeof(client_t));
    if (!client) {
        ERROR("malloc() failed");
        return NULL;
    }

    client->stream_fd = stream_fd;
    inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, client->addr, INET_ADDRSTRLEN);
    client->port = ntohs(client_addr.sin_port);

    INFO("new client connection from %s:%d", client->addr, client->port);

    return client;
}

void on_client_exit(client_t *client) {
    if (pthread_mutex_lock(&clients_mutex)) {
        ERROR("pthread_mutex_lock() failed");
    }

    int i, j;
    for (i = 0; i < num_clients; i++) {
        if (clients[i] == client) {
            /* move all further entries back with one position */
            for (j = i; j < num_clients - 1; j++) {
                clients[j] = clients[j + 1];
            }

            break;
        }
    }

    clients = realloc(clients, sizeof(client_t *) * (--num_clients));

    DEBUG("current clients: %d", num_clients);

    if (pthread_mutex_unlock(&clients_mutex)) {
        ERROR("pthread_mutex_unlock() failed");
    }
}


    /* main */

char *str_timestamp() {
    static __thread char s[20];

    time_t t = time(NULL);
    struct tm *tmp = localtime(&t);

    strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S", tmp);

    return s;
}

void print_help() {
    fprintf(stderr, "\n");
    fprintf(stderr, "streamEye %s\n\n", STREAM_EYE_VERSION);
    fprintf(stderr, "Usage: <jpeg stream> | streameye [options]\n");
    fprintf(stderr, "Available options:\n");
    fprintf(stderr, "    -d                 debug mode, increased log verbosity\n");
    fprintf(stderr, "    -h                 print this help text\n");
    fprintf(stderr, "    -l                 listen only on localhost interface\n");
    fprintf(stderr, "    -p port            tcp port to listen on (defaults to %d)\n", DEF_TCP_PORT);
    fprintf(stderr, "    -q                 quiet mode, log only errors\n");
    fprintf(stderr, "    -s separator       a separator between jpeg frames received at input\n");
    fprintf(stderr, "                       (will autodetect jpeg frame starts by default)\n");
    fprintf(stderr, "    -t timeout         client read timeout, in seconds (defaults to %d)\n", DEF_CLIENT_READ_TIMEOUT);
    fprintf(stderr, "\n");
}

void bye_handler(int signal) {
    if (!running) {
        INFO("interrupt already received, ignoring signal");
        return;
    }

    INFO("interrupt received, quitting");
    running = 0;
}

int main(int argc, char *argv[]) {

    /* read command line arguments */
    int c;
    char *err = NULL;

    opterr = 0;
    while ((c = getopt(argc, argv, "dhlp:qs:t:")) != -1) {
        switch (c) {
            case 'd': /* debug */
                log_level = 2;
                break;

            case 'h': /* help */
                print_help();
                return 0;

            case 'l': /* listen on localhost */
                listen_localhost = 1;
                break;

            case 'p': /* tcp port */
                tcp_port = strtol(optarg, &err, 10);
                if (*err != 0) {
                    ERROR("invalid port \"%s\"", optarg);
                    return -1;
                }
                break;

            case 'q': /* quiet */
                log_level = 0;
                break;

            case 's': /* input separator */
                input_separator = strdup(optarg);
                break;

            case 't': /* client timeout */
                client_read_timeout = strtol(optarg, &err, 10);
                if (*err != 0) {
                    ERROR("invalid client timeout \"%s\"", optarg);
                    return -1;
                }
                break;

            case '?':
                ERROR("unknown or incomplete option \"-%c\"", optopt);
                return -1;

            default:
                print_help();
                return -1;
        }
    }

    if (!tcp_port) {
        tcp_port = DEF_TCP_PORT;
    }

    INFO("streamEye %s", STREAM_EYE_VERSION);
    INFO("hello!");

    if (input_separator && strlen(input_separator) < 4) {
        INFO("the input separator supplied is very likely to appear in the actual frame data (consider a longer one)");
    }

    /* signals */
    DEBUG("installing signal handlers");
    struct sigaction act;
    act.sa_handler = bye_handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);

    if (sigaction(SIGINT, &act, NULL) < 0) {
        ERRNO("sigaction() failed");
        return -1;
    }
    if (sigaction(SIGTERM, &act, NULL) < 0) {
        ERRNO("sigaction() failed");
        return -1;
    }
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        ERRNO("signal() failed");
        return -1;
    }

    /* threading */
    DEBUG("initializing thread synchronization");
    if (pthread_cond_init(&jpeg_cond, NULL)) {
        ERROR("pthread_cond_init() failed");
        return -1;
    }
    if (pthread_mutex_init(&jpeg_mutex, NULL)) {
        ERROR("pthread_mutex_init() failed");
        return -1;
    }
    if (pthread_mutex_init(&clients_mutex, NULL)) {
        ERROR("pthread_mutex_init() failed");
        return -1;
    }

    /* tcp server */
    DEBUG("starting server");
    int socket_fd = init_server();
    if (socket_fd < 0) {
        ERROR("failed to start server");
        return -1;
    }

    INFO("listening on %s:%d", listen_localhost ? "127.0.0.1" : "0.0.0.0", tcp_port);

    /* start with the mutex locked */
    if (pthread_mutex_lock(&jpeg_mutex)) {
        ERROR("pthread_mutex_lock() failed");
        return -1;
    }

    /* main loop */
    char input_buf[INPUT_BUF_LEN];
    char *sep;
    int size, rem_len, i;
    int auto_separator = 0;
    int input_separator_len;
    if (!input_separator) {
        auto_separator = 1;
        input_separator_len = 4; /* strlen(JPEG_START) + strlen(JPEG_END) */;
        input_separator = malloc(input_separator_len + 1);
        snprintf(input_separator, input_separator_len + 1, "%s%s", JPEG_END, JPEG_START);
    }
    else {
        input_separator_len = strlen(input_separator);
    }

    while (running) {
        size = read(STDIN_FILENO, input_buf, INPUT_BUF_LEN);
        if (size < 0) {
            if (errno == EINTR) {
                break;
            }

            ERRNO("input: read() failed");
            return -1;
        }
        else if (size == 0) {
            DEBUG("input: end of stream");
            running = 0;
            break;
        }

        if (size > JPEG_BUF_LEN - 1 - jpeg_size) {
            ERROR("input: jpeg size too large, discarding buffer");
            jpeg_size = 0;
            continue;
        }

        memcpy(jpeg_buf + jpeg_size, input_buf, size);
        jpeg_size += size;

        /* look behind at most 2 * INPUT_BUF_LEN for a separator */
        sep = (char *) memmem(jpeg_buf + jpeg_size - MIN(2 * INPUT_BUF_LEN, jpeg_size), MIN(2 * INPUT_BUF_LEN, jpeg_size),
                input_separator, input_separator_len);

        if (sep) {
            if (auto_separator) {
                rem_len = jpeg_size - (sep - jpeg_buf) - 2 /* strlen(JPEG_START) */;
                jpeg_size = sep - jpeg_buf + 2 /* strlen(JPEG_END) */;
            }
            else {
                rem_len = jpeg_size - (sep - jpeg_buf) - input_separator_len;
                jpeg_size = sep - jpeg_buf;
            }

            DEBUG("input: jpeg buffer ready with %d bytes", jpeg_size);

            for (i = 0; i < num_clients; i++) {
                clients[i]->jpeg_ready = 1;
            }

            if (pthread_cond_broadcast(&jpeg_cond)) {
                ERROR("pthread_cond_broadcast() failed");
                return -1;
            }

            if (pthread_mutex_unlock(&jpeg_mutex)) {
                ERROR("pthread_mutex_unlock() failed");
                return -1;
            }

            /* this allows clients to acquire the lock
             * for a small period of time and do their thing */
            usleep(1000);

            if (pthread_mutex_lock(&jpeg_mutex)) {
                ERROR("pthread_mutex_lock() failed");
                return -1;
            }

            /* copy the remainder of data back to the jpeg buffer */
            memmove(jpeg_buf, sep + (auto_separator ? 2 /* strlen(JPEG_END) */ : input_separator_len), rem_len);
            jpeg_size = rem_len;
        }

        /* look for incoming clients */
        client_t *client = wait_for_client(socket_fd);
        if (client) {
            if (pthread_create(&client->thread, NULL, (void *(*) (void *)) handle_client, client)) {
                ERROR("pthread_create() failed");
                return -1;
            }

            if (pthread_mutex_lock(&clients_mutex)) {
                ERROR("pthread_mutex_lock() failed");
                return -1;
            }

            clients = realloc(clients, sizeof(client_t *) * (num_clients + 1));
            clients[num_clients++] = client;

            DEBUG("current clients: %d", num_clients);

            if (pthread_mutex_unlock(&clients_mutex)) {
                ERROR("pthread_mutex_unlock() failed");
                return -1;
            }
        }
    }

    DEBUG("closing server");
    close(socket_fd);

    DEBUG("waiting for clients to finish");
    for (i = 0; i < num_clients; i++) {
        clients[i]->running = 0;
        clients[i]->jpeg_ready = 1;
    }

    if (pthread_cond_broadcast(&jpeg_cond)) {
        ERROR("pthread_cond_broadcast() failed");
        return -1;
    }

    if (pthread_mutex_unlock(&jpeg_mutex)) {
        ERROR("pthread_mutex_unlock() failed");
        return -1;
    }

    for (i = 0; i < num_clients; i++) {
        pthread_join(clients[i]->thread, NULL);
    }

    if (pthread_mutex_destroy(&clients_mutex)) {
        ERROR("pthread_mutex_destroy() failed");
        return -1;
    }
    if (pthread_mutex_destroy(&jpeg_mutex)) {
        ERROR("pthread_mutex_destroy() failed");
        return -1;
    }
    if (pthread_cond_destroy(&jpeg_cond)) {
        ERROR("pthread_cond_destroy() failed");
        return -1;
    }

    INFO("bye!");

    return 0;
}
