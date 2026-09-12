/*
 * chat_socket.c
 *
 * A TCP client/server chat program built directly on the POSIX sockets API.
 *
 * Usage:
 *   ./chat_socket 1                 -> run as server (listens on MYPORT)
 *   ./chat_socket 2 <server_ip>     -> run as client (connects to server_ip:MYPORT)
 *
 * Protocol:
 *   - Messages are newline-delimited: each line typed by the client is sent
 *     as-is (including the trailing '\n'), and the server frames incoming
 *     bytes on '\n' to reconstruct complete messages, since TCP is a byte
 *     stream and gives no guarantee that one send() lines up with one recv().
 *   - The client can end its session by sending the literal message "!q".
 *
 * Concurrency:
 *   - The server accepts connections in a loop and fork()s a dedicated
 *     child process per client, so multiple clients can be connected and
 *     chatting at the same time without blocking one another.
 *
 * Error handling:
 *   - Every syscall that can fail is wrapped in a capitalized helper
 *     (Getaddrinfo, Socket, Bind, ...) that checks the return value,
 *     reports the error via errno/gai_strerror, and exits on failure -
 *     keeping main() focused on program flow rather than repeated checks.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>

#define MYPORT   "3490" /* Port the server listens on / the client connects to */
#define BUF_SIZE 500    /* Size of the raw recv/send and message-accumulator buffers */

/* ---------------------------------------------------------------------- */
/* Error-checked wrappers around the standard socket calls                */
/* ---------------------------------------------------------------------- */
int     Getaddrinfo(const char *name, const char *service,
                     const struct addrinfo *req, struct addrinfo **pai);
int     Socket(int domain, int type, int protocol);
int     Bind(int fd, const struct sockaddr *addr, socklen_t len);
int     Listen(int fd, int backlog);
int     Connect(int fd, const struct sockaddr *addr, socklen_t len);
int     Accept(int fd, struct sockaddr *addr, socklen_t *addr_len);
ssize_t Recv(int fd, void *buf, size_t len, int flags);
ssize_t Send(int fd, const void *buf, size_t len, int flags);

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: ./chat_socket [1]->server / [2]->client [ip_address]->client only\n");
        exit(EXIT_FAILURE);
    }

    struct addrinfo hints;              /* Criteria used to resolve/filter addresses */
    struct addrinfo *serverinfo;        /* Linked list of results from getaddrinfo() */
    struct sockaddr_storage their_addr; /* Connecting peer's address (server side) */
    socklen_t addr_size;
    int sockfd;
    int new_sockfd;
    int max_con = 20;
    int bytesrecieved;
    char buf[BUF_SIZE];  /* Raw buffer for a single recv()/fgets() call */
    char buf2[BUF_SIZE]; /* Accumulator for one complete message (server side) */

    /* ------------------------------------------------------------------ */
    /* SERVER MODE                                                        */
    /* ------------------------------------------------------------------ */
    if ((strcmp(argv[1], "1")) == 0)
    {
        fprintf(stdout, "Opção: Servidor\n");

        /* Build address hints: any local IP, TCP, using our fixed port */
        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = AI_PASSIVE; /* Fill in my IP for me */
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = 0;

        Getaddrinfo(NULL, MYPORT, &hints, &serverinfo);
        sockfd = Socket(serverinfo->ai_family, serverinfo->ai_socktype, serverinfo->ai_protocol);
        Bind(sockfd, serverinfo->ai_addr, serverinfo->ai_addrlen);

        /* Address info is no longer needed once we're bound */
        freeaddrinfo(serverinfo);

        Listen(sockfd, max_con);
        printf("Server: Listening for connections...\n");

        while (1)
        {
            addr_size = sizeof(their_addr);
            new_sockfd = Accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);

            if (fork() == 0)
            {
                /* --- Child process: handles exactly one client --- */
                close(sockfd); /* the listening socket belongs to the parent */

                size_t n = 0; /* Number of bytes currently held in buf2 */
                memset(&buf2, 0, sizeof(buf2));

                while (1)
                {
                    bytesrecieved = Recv(new_sockfd, buf, sizeof(buf), 0);

                    /* Walk each byte just received and reassemble it into buf2
                     * until a '\n' marks a complete message (TCP has no message
                     * boundaries of its own, so framing is done here). */
                    for (int i = 0; i < bytesrecieved; i++)
                    {
                        /* Guard against a message (or malicious input) with no
                         * '\n' that would otherwise overflow buf2 */
                        if (n >= BUF_SIZE - 1)
                        {
                            printf("Message too large or someone being naughty\n");
                            close(new_sockfd);
                            return 1;
                        }

                        if (buf[i] == '\n')
                        {
                            /* Check for the quit command before printing/echoing it */
                            if ((strcmp(buf2, "!q")) == 0)
                            {
                                fprintf(stdout, "Client Closed Connection!\n");
                                close(new_sockfd);
                                return 0;
                            }

                            printf("Client message: %s\n", buf2);

                            /* Clear only the bytes that were written, then reset */
                            memset(&buf2, 0, n);
                            n = 0;
                        }
                        else
                        {
                            buf2[n] = buf[i];
                            n++;
                        }
                    }

                    /* recv() returns 0 when the peer closed the connection (FIN) */
                    if (bytesrecieved == 0)
                    {
                        fprintf(stdout, "Connection Terminated");
                        close(new_sockfd);
                        return 0;
                    }
                }
            }

            /* --- Parent process: done with this client, back to accept() --- */
            close(new_sockfd);
        }
    }

    /* ------------------------------------------------------------------ */
    /* CLIENT MODE                                                        */
    /* ------------------------------------------------------------------ */
    if ((strcmp(argv[1], "2")) == 0)
    {
        if (argc != 3)
        {
            fprintf(stderr, "Usage: ./chat_socket [1]->server / [2]->client [ip_address]->client only\n");
            return 1;
        }

        fprintf(stdout, "Opção: Cliente\n");

        /* Build address hints for the target server (no AI_PASSIVE: we're connecting out) */
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = 0;

        Getaddrinfo(argv[2], MYPORT, &hints, &serverinfo);
        sockfd = Socket(serverinfo->ai_family, serverinfo->ai_socktype, serverinfo->ai_protocol);

        /* No bind() here: connect() lets the kernel pick our local address/port,
         * which is what we want as a client */
        Connect(sockfd, serverinfo->ai_addr, serverinfo->ai_addrlen);

        freeaddrinfo(serverinfo);

        while (1)
        {
            memset(&buf, 0, sizeof(buf));
            printf("Type message to send: ");
            fgets(buf, sizeof(buf), stdin);

            ssize_t n;
            size_t x = 0; /* Total bytes sent so far for this message */

            /* Quit command: send it like any other message, then close and exit */
            if ((strcmp(buf, "!q\n")) == 0)
            {
                do
                {
                    n = Send(sockfd, (buf + x), (strlen(buf) - x), 0);
                    x += n;
                } while (x != strlen(buf));

                printf("Connection Closed\n");
                close(sockfd);
                return 0;
            }

            /* send() isn't guaranteed to send everything in one call, so loop
             * until the whole message (x bytes) has actually gone out */
            do
            {
                n = Send(sockfd, (buf + x), (strlen(buf) - x), 0);
                x += n;
            } while (x != strlen(buf));

            printf("Sent!\n");
        }
    }

    return 0;
}

/* ---------------------------------------------------------------------- */
/* Wrapper implementations                                                */
/* ---------------------------------------------------------------------- */

int Getaddrinfo(const char *name, const char *service,
                 const struct addrinfo *req, struct addrinfo **pai)
{
    int n;
    if ((n = getaddrinfo(name, service, req, pai)) != 0)
    {
        fprintf(stderr, "Erro no getaddrinfo() : %s\n", gai_strerror(n));
        exit(EXIT_FAILURE);
    }
    return n;
}

int Socket(int domain, int type, int protocol)
{
    int n;
    if ((n = socket(domain, type, protocol)) == -1)
    {
        fprintf(stderr, "Erro a criar socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return n;
}

int Bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    int n;
    if ((n = bind(fd, addr, len)) == -1)
    {
        fprintf(stderr, "Erro a dar bind: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return n;
}

int Listen(int fd, int backlog)
{
    int n;
    if ((n = listen(fd, backlog)) == -1)
    {
        fprintf(stderr, "Erro a dar listen: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return n;
}

int Connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    int n;
    if ((n = connect(fd, addr, len)) == -1)
    {
        fprintf(stderr, "Erro a dar connect: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return n;
}

int Accept(int fd, struct sockaddr *addr, socklen_t *addr_len)
{
    int n;
    if ((n = accept(fd, addr, addr_len)) == -1)
    {
        fprintf(stderr, "Erro a dar accept: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return n;
}

ssize_t Send(int fd, const void *buf, size_t len, int flags)
{
    ssize_t n;
    if ((n = send(fd, buf, len, flags)) == -1)
    {
        fprintf(stderr, "Erro sending: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return n;
}

ssize_t Recv(int fd, void *buf, size_t len, int flags)
{
    ssize_t n;
    if ((n = recv(fd, buf, len, flags)) == -1)
    {
        fprintf(stderr, "Erro receaving %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return n;
}