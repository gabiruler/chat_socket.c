# chat-socket

A minimal TCP client/server chat application written in C, built directly on the POSIX sockets API (`getaddrinfo`, `socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`) — no libraries, no frameworks, just the raw syscalls that every higher-level networking library is built on top of.

This project was built to get hands-on with how network programs actually behave at the byte level: connection setup, partial reads/writes, and the fact that **TCP has no concept of "messages"** — just a stream of bytes that your application has to interpret correctly.

## Why this project

I'm currently studying cybersecurity, and I wanted to complement that with a solid, ground-up understanding of how the network layer actually works — not just how to configure or defend it, but how the protocols and system calls behave from the inside. Writing a socket program from scratch surfaces a lot of the same issues (buffer handling, malformed input, protocol framing, denial-of-service via resource exhaustion) that show up in real security work, just from the builder's side of the fence instead of the attacker's.

## Features

- **Single binary, dual mode** — the same executable runs as either the server or the client, selected via a command-line argument.
- **Concurrent client handling** — the server loops on `accept()` and `fork()`s a dedicated child process per connection, so multiple clients can be connected and chatting at the same time instead of the server blocking on a single conversation.
- **Message framing over a raw byte stream** — TCP delivers bytes reliably and in order, but makes *no guarantee* that a single `send()` call corresponds to a single `recv()` call on the other end. This program frames messages using a `\n` delimiter and accumulates bytes across as many `recv()` calls as it takes to see one, correctly handling messages that arrive split across multiple reads or multiple messages arriving in a single read.
- **Partial-send-safe writes** — `send()` is not guaranteed to transmit an entire buffer in one call. The client loops on `send()`, tracking exactly how many bytes have gone out, until the full message is confirmed sent.
- **Overflow-guarded message accumulation** — the server enforces a hard cap on how many bytes it will buffer while waiting for a message delimiter, so a malformed or hostile client (e.g. connecting directly with `netcat` and sending an unbounded stream with no `\n`) can't overrun the accumulation buffer.
- **Graceful shutdown command** — the client can end its session cleanly by sending `!q`, which closes the socket properly and lets the server log an intentional disconnect ("Client Closed Connection!") distinctly from a dropped or crashed connection ("Connection Terminated").
- **Centralized error handling** — every syscall that can fail (`getaddrinfo`, `socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`) is wrapped in a small capitalized helper (`Getaddrinfo`, `Socket`, ...) that checks the return value, reports the error via `errno`/`gai_strerror`, and exits on failure — keeping `main()` focused on program flow instead of repeated error-checking boilerplate.

## How it works

```
 Server (parent)                          Client
 ---------------                          ------
 getaddrinfo(NULL, PORT)                  getaddrinfo(server_ip, PORT)
 socket() -> bind() -> listen()           socket()
 accept()  <-------------------------->   connect()
      |
      +-- fork() --> Server (child, one per client)
      |                   |                    |
      |                   |<---- send("message\n") ---- | (loops until fully sent)
      |              recv() accumulates bytes until '\n'
      |              prints complete message
      |                   |
      |                   |<------- send("!q\n") ------ |
      |              child closes its socket and exits
      |
 loop back to accept() for the next client
```

## Building

```bash
gcc -Wall -Wextra -o chat_socket chat_socket.c
```

## Usage

**Start the server** (listens on port 3490):
```bash
./chat_socket 1
```

**Connect a client** (from the same or a different machine on the network):
```bash
./chat_socket 2 <server_ip>
```

Type a message and press Enter to send it. Type `!q` on client side to close the connection cleanly.

## What I'd add next

- Reaping finished child processes via a `SIGCHLD` handler and `waitpid()` with `WNOHANG`, to avoid accumulating zombie processes on a long-running server
- Non-blocking I/O (or an event loop with `select()`/`poll()`/`epoll()`) as an alternative concurrency model to forking, for handling far larger numbers of simultaneous clients
- A lightweight length-prefixed or JSON-based message protocol instead of plain newline delimiting, for structured data exchange
- Basic transport encryption (TLS via OpenSSL) as a natural next step given the security focus

## Background

Built while studying low-level network programming in C, following *Beej's Guide to Network Programming*, *Advanced Programming in the UNIX Environment*, and *UNIX Network Programming*.
