#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>

////////////////////////////////////////////////////////
// global variable
////////////////////////////////////////////////////////

int         g_server_fd;

fd_set      g_master;
int         g_fd_max;

typedef struct s_fd_data {
    int     id;
    char    *buff;
} t_fd_data;

t_fd_data   g_fd_database[FD_SETSIZE];
int         g_next_id = 0;


////////////////////////////////////////////////////////
// Provided code
////////////////////////////////////////////////////////
int extract_message(char **buf, char **msg)
{
    printf("extract_message\n");
    char	*newbuf;
    int	i;

    *msg = 0;
    if (*buf == 0)
        return (0);
    i = 0;
    while ((*buf)[i])
    {
        if ((*buf)[i] == '\n')
        {
            newbuf = (char *)calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
            if (newbuf == 0)
                return (-1);
            strcpy(newbuf, *buf + i + 1);
            *msg = *buf;
            (*msg)[i + 1] = 0;
            *buf = newbuf;
            return (1);
        }
        i++;
    }
    return (0);
}

char *str_join(char *buf, char *add)
{
    printf("str_join\n");
    char	*newbuf;
    int		len;

    if (buf == 0)
        len = 0;
    else
        len = strlen(buf);
    newbuf = (char *)malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
    if (newbuf == 0)
        return (0);
    newbuf[0] = 0;
    if (buf != 0)
        strcat(newbuf, buf);
    free(buf);
    strcat(newbuf, add);
    return (newbuf);
}

////////////////////////////////////////////////////////
// utils
////////////////////////////////////////////////////////
void fatal_error() {
    printf("fatal_error\n");
    write(2, "Fatal error\n", 12);
    exit(1);
}

void cleanup() {
    printf("cleanup\n");
    for (int fd = 0; fd <= g_fd_max; ++fd) {
        if (fd != g_server_fd && FD_ISSET(fd, &g_master)) {
            FD_CLR(fd, &g_master);
            free(g_fd_database[fd].buff);
            g_fd_database[fd].buff = NULL;
            g_fd_database[fd].id = -1;
            close(fd);
        }
    }
    FD_CLR(g_server_fd, &g_master);
    close(g_server_fd);
}

void cleanup_fatal_error() {
    printf("cleanup_fatal_error\n");
    cleanup();
    fatal_error();
}

void broadcast(int sender_fd, const char *message) {
    printf("broadcast\n");
    if (message == NULL)
        return ;
    for (int fd = 0; fd <= g_fd_max; ++fd) {
        if (fd != g_server_fd
            && fd != sender_fd
            && FD_ISSET(fd, &g_master))
            send(fd, message, strlen(message), 0);
    }
}


////////////////////////////////////////////////////////
// handle_new_connection
////////////////////////////////////////////////////////
void handle_new_connection(void) {
    printf("handle_new_connection\n");
    int new_client_fd = accept(g_server_fd, NULL, NULL);
    if (new_client_fd > 0) {
        // setup
        FD_SET(new_client_fd, &g_master);
        if (g_fd_max < new_client_fd)
            g_fd_max = new_client_fd;
        g_fd_database[new_client_fd].id = g_next_id;
        ++g_next_id;
        g_fd_database[new_client_fd].buff = NULL;
        // broadcast
        char message[64];
        sprintf(message, "server: client %d just arrived\n", g_fd_database[new_client_fd].id);
        broadcast(new_client_fd, message);
    }
}

////////////////////////////////////////////////////////
// handle_new_connection
////////////////////////////////////////////////////////
void handle_disconnection(int client_fd) {
    printf("handle_disconnection\n");
    // broadcast
    char message[64];
    sprintf(message, "server: client %d just left\n", g_fd_database[client_fd].id);
    broadcast(client_fd, message);

    // cleanup
    FD_CLR(client_fd, &g_master);

    free(g_fd_database[client_fd].buff);
    g_fd_database[client_fd].buff = NULL;
    g_fd_database[client_fd].id = -1;

    close(client_fd);
}

void handle_received_data(int client_fd) {
    printf("handle_received_data\n");
    char temp[4096];
    // -1!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    ssize_t bytes = recv(client_fd, temp, sizeof(temp) - 1, 0);
    if (bytes == 0) {
        handle_disconnection(client_fd);
    } else if (bytes > 0) {
        temp[bytes] = '\0';
        char *new_buff;
        new_buff = str_join(g_fd_database[client_fd].buff, temp);
        if (new_buff == NULL)
            cleanup_fatal_error();
        // g_fd_database[client_fd].buff)はstr_join()内でfreeされる
        g_fd_database[client_fd].buff = new_buff;
        char *message = NULL;
        while (1) {
            int ret = extract_message(&g_fd_database[client_fd].buff, &message);
            if (ret > 0) {

                char *prefix = (char *)calloc(16, sizeof(char));
                if (prefix == NULL) {
                    free(message);
                    cleanup_fatal_error();
                }
                sprintf(prefix, "client %d: ", g_fd_database[client_fd].id);

                char *message_with_prefix = str_join(prefix, message);
                // prefixはfreeされる
                prefix = NULL;
                free(message);
                message = NULL;
                if (message_with_prefix == NULL)
                    cleanup_fatal_error();

                broadcast(client_fd, message_with_prefix);
                free(message_with_prefix);
                message_with_prefix = NULL;

            } else if (ret == 0) {
                return;
            } else {
                cleanup_fatal_error();
            }
        }
    }
}

////////////////////////////////////////////////////////
// main
////////////////////////////////////////////////////////
int main(int argc, char **argv) {

    if (argc < 2) {
        write(2, "Wrong number of arguments\n", 26);
        return 1;
    }

    // Create socket
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0)
        fatal_error();

    // Bind newly created socket to given IP
    struct sockaddr_in sockaddr;
    bzero(&sockaddr, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sockaddr.sin_port = htons(atoi(argv[1]));
    if (bind(g_server_fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0) {
        close(g_server_fd);
        fatal_error();
    }

    // Start listening
    if (listen(g_server_fd, 128) < 0) {
        close(g_server_fd);
        fatal_error();
    }

    // Prepare
    FD_ZERO(&g_master);
    FD_SET(g_server_fd, &g_master);
    g_fd_max = g_server_fd;

    // Start main loop
    while (1) {
        fd_set read_fds = g_master;
        if (select(g_fd_max + 1, &read_fds, NULL, NULL, NULL) > 0) {
            for (int fd = 0; fd <= g_fd_max; ++fd) {
                if (FD_ISSET(fd, &read_fds)) {
                    if (fd == g_server_fd) {
                        handle_new_connection();
                    } else {
                        handle_received_data(fd);
                    }
                }
            }
        }
    }

    cleanup();
    return 0;
}
