/*
 * chat_socket.c
 *
 * A minimal TCP client/server chat program built on the BSD sockets API.
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
 *   - Client side can end the session by sending the literal message "!q".
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

#define MYPORT "3490"     /* Port the server listens on / the client connects to */
#define BUF_SIZE 500      /* Size of the raw recv/send and message-accumulator buffers */

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: ./chat_socket [1]->server / [2]->client [ip_address]->client only\n");
        exit(EXIT_FAILURE);
    }

    struct addrinfo hints;             /* Criteria used to resolve/filter addresses */
    struct addrinfo *serverinfo;       /* Linked list of results from getaddrinfo() */
    struct sockaddr_storage their_addr;/* Connecting peer's address (server side) */
    socklen_t addr_size;
    int status, sockfd;
    int new_sockfd;
    int bytesrecieved;
    char buf[BUF_SIZE];                /* Raw buffer for a single recv()/fgets() call */
    char buf2[BUF_SIZE];               /* Accumulator for one complete message (server side) */

    /* ------------------------------------------------------------------ */
    /* SERVER MODE                                                        */
    /* ------------------------------------------------------------------ */
    if ((strcmp(argv[1], "1")) == 0)
    {
        fprintf(stdout, "Opção: Servidor\n");

        /* Build address hints: any local IP, TCP, using our fixed port */
        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = AI_PASSIVE;     /* Fill in my IP for me */
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = 0;

        if ((status = getaddrinfo(NULL, MYPORT, &hints, &serverinfo)) != 0)
        {
            fprintf(stderr, "Erro no getaddrinfo() : %s\n", gai_strerror(status));
            exit(EXIT_FAILURE);
        }

        if ((sockfd = socket(serverinfo->ai_family, serverinfo->ai_socktype, serverinfo->ai_protocol)) == -1)
        {
            fprintf(stderr, "Erro a criar socket: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if ((bind(sockfd, serverinfo->ai_addr, serverinfo->ai_addrlen)) == -1)
        {
            fprintf(stderr, "Erro a dar bind: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if ((listen(sockfd, 5)) == -1)
        {
            fprintf(stderr, "Erro a dar listen: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        printf("Server: Waiting for connections...\n");

        addr_size = sizeof(their_addr);
        if ((new_sockfd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size)) == -1)
        {
            fprintf(stderr, "Erro a dar accept: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        /* Address info is no longer needed once we're bound/listening/accepted */
        freeaddrinfo(serverinfo);

        size_t n = 0;                     /* Number of bytes currently held in buf2 */
        memset(&buf2, 0, sizeof(buf2));

        while (1)
        {
            bytesrecieved = recv(new_sockfd, buf, sizeof(buf), 0);

            /* Walk each byte just received and reassemble it into buf2 until
             * a '\n' marks a complete message (TCP has no message boundaries
             * of its own, so this framing is done at the application level). */
            for (int i = 0; i < bytesrecieved; i++)
            {
                /* Guard against a message (or malicious input) with no '\n'
                 * that would otherwise overflow buf2 */
                if (n >= BUF_SIZE - 1)
                {
                    printf("Message too large or someone being naughty\n");
                    close(new_sockfd);
                    close(sockfd);
                    return 1;
                }

                if (buf[i] == '\n')
                {
                    /* Check for the quit command before printing/echoing it */
                    if ((strcmp(buf2, "!q")) == 0)
                    {
                        close(new_sockfd);
                        close(sockfd);
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

            /* recv() returns 0 when the peer closed the connection (FIN),
             * or a negative value on error */
            if (bytesrecieved <= 0)
            {
                close(new_sockfd);
                close(sockfd);
                break;
            }
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

        if ((status = getaddrinfo(argv[2], MYPORT, &hints, &serverinfo)) != 0)
        {
            fprintf(stderr, "Erro no getaddrinfo() : %s\n", gai_strerror(status));
            exit(EXIT_FAILURE);
        }

        if ((sockfd = socket(serverinfo->ai_family, serverinfo->ai_socktype, serverinfo->ai_protocol)) == -1)
        {
            fprintf(stderr, "Erro a criar socket: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        /* No bind() here: connect() lets the kernel pick our local address/port,
         * which is what we want as a client */
        if ((connect(sockfd, serverinfo->ai_addr, serverinfo->ai_addrlen)) == -1)
        {
            fprintf(stderr, "Erro a dar connect: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        freeaddrinfo(serverinfo);

        while (1)
        {
            memset(&buf, 0, sizeof(buf));
            printf("Type message to send: ");
            fgets(buf, sizeof(buf), stdin);

            size_t n;
            size_t x = 0; /* Total bytes sent so far for this message */

            /* Quit command: send it like any other message, then close and exit */
            if ((strcmp(buf, "!q\n")) == 0)
            {
                do
                {
                    n = send(sockfd, (buf + x), (strlen(buf) - x), 0);
                    if (n < 0)
                    {
                        fprintf(stderr, "Erro sending: %s\n", strerror(errno));
                        exit(EXIT_FAILURE);
                    }
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
                n = send(sockfd, (buf + x), (strlen(buf) - x), 0);
                if (n < 0)
                {
                    fprintf(stderr, "Erro sending: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                x += n;
            } while (x != strlen(buf));

            printf("Sent!\n");
        }
    }

    return 0;
}