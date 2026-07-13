# SocketHttp(server|client) based on IHttpRequest(Server|Client)

Implementations in `AsyncSocket/AsyncSocket_Http(Server|Client)Api.(h|cpp)`
Using namespace `vl::inter_process::async_tcp_socket`

- SocketHttpServer / SocketHttpClient
  - On Windows, use socket vs http api in unit test.
  - Test app hosts http service in two different ports
    - JS from one service calls another service
    - Test against Windows(Chrome), Ubuntu(firefox), macOS(safari)
  - Multiple server on one port share the same IHttpRequestServer.
    - A spin lock protects a global map pointer.
    - each item is refcount protected, released automatically.
    - the whole map is refcount protected, released automatically.
    - If creating socket server fails because of port is occupied:
      - server should take a look at the map again to see if one has been created.
      - if not created retry, in total 5 times.
      - creating socket server should not hold the spin lock.

## Must-Have features to let Firefox/Chrome/Safari interact with a minimized Http service

<!-- summarization of must-have features according -->

### HTTP service as RESTful API

### HTTP service hosting a website

### XSS and Options verb

### How to tell the browser if anything is not supported

<!-- one direction: can't decode gzip -->
<!-- another direction: force browser to only use unicode, so we don't need to deal with others -->
