RMBT Test Server
================

> This project contains the RMBT Test Server for conducting measurements based on 
  the RMBT protocol. Clients can communicate either directly via TCP sockets or based on 
  the WebSocket protocol.

Docker
------

### Server

To simply run the server (**with diabled token check!**) with Docker, run:

```
docker run -v rmbtd-config:/config -p 8081-8082:8081-8082 rtrnettest/rmbt-server
```

Prerequisites
-------------

Needed packages:
- build-essential
- libssl-dev

Usage
-----

Generate the random file, your secret key and the server executable using

```
> make random
> make key
> make all
```

For production use, an optimized build can be generated using ```make server-prod```.

The server uses keys to authenticate clients, as specified in the RMBT protocol. These keys that 
are loaded from the `secret.key` file at startup. Each line in this file contains the key as well 
as a space-separated label, which will be sent to the syslog each time a client identifies 
using this key.

For running the server, either use the supplied /etc/systemd/system service scripts or the ```rmbtd``` executable.

Parameters:

```
==== rmbtd ====
command line arguments:

 -l/-L  listen on (IP and) port; -L for SSL;
        examples: "443","1.2.3.4:1234","[2001:1234::567A]:1234"
        maybe specified multiple times; at least once

 -c     path to SSL certificate in PEM format;
        intermediate certificates following server cert in same file if needed

 -k     path to SSL key file in PEM format; required

 -t     number of worker threads to run for handling connections (default: 200)

 -u     drop root privileges and setuid to specified user; must be root

 -d     fork into background as daemon (no argument)

 -D     enable debug logging (no argument)

 -w     use as http and websocket server (no argument)
 
 -v     behave as version (v) for serving older clients
        example: "0.3"

```

Client communication
--------------------

The server can be used in HTTP mode or in the legacy plain mode, depending on the usage of the ```-w``` parameter.

### Usage in HTTP or WebSocket mode

If the ```-w``` parameter is supplied when starting the server, the server will listen for
HTTP GET requests on the specified TCP port. Clients can request a connection upgrade to
either ```RMBT``` or ```websocket``` by sending a ```Connection: Upgrade``` header
as specified in RFC 2616.


#### HTTP Upgrade to RMBT

Clients can request an upgrade plain RMBT by adding a ```Upgrade: RMBT``` header. A sample 
communication is given below. Additionally, the client may add an ```RMBT-Version```-header, 
indicating the most recent compatible server version.


Request from a client to the server
```
GET /rmbt HTTP/1.1
Connection: Upgrade
Upgrade: RMBT
RMBT-Version: 1.2.0
```

Response from the server
```
HTTP/1.1 101 Switching Protocols
Connection: Upgrade
Upgrade: RMBT
```

After this handshake is complete, the communication will continue using plain RMBT.

#### HTTP Upgrade to WebSocket

Clients can request RMBT communication wrapped in the WebSocket protocol. For this,
a handshake as specified in RFC 6455 is used. A sample communication is given below.

Request from a client to the server
```
GET /rmbt HTTP/1.1
Connection: Upgrade
Upgrade: websocket
Sec-WebSocket-Version: 13
Sec-WebSocket-Key: 38VqBEsiw/GKJUPnSGNAUA==
```

Response from the server
```
HTTP/1.1 101 Switching Protocols
Connection: Upgrade
Sec-WebSocket-Accept: V8nixtUGE3Gfzy3Qix9R0svp05M=
Upgrade: websocket
```

After this handshake is complete, the communication will continue using RMBT wrapped
in WebSocket frames.  

### Usage without HTTP

If the server is started without the ```-w``` parameter, communication is done by relying 
on plain TCP sockets. As soon as the connection is established, the server will initiate
the communication by sending the string ```RMBT<VERSION>``` whereas ```<VERSION>``` will contain
the current version of the server software, e.g. ```v1.2.0```.

Related materials
-----------------

* [RMBT specification](https://www.netztest.at/doc/)
* [RMBTws Client](https://github.com/rtr-nettest/rmbtws)
* [RTR-Netztest/open-rmbt](https://github.com/rtr-nettest/open-rmbt)
* [RMBT C client (3<sup>rd</sup> party)](https://github.com/lwimmer/rmbt-client)  

Get in Touch
------------

* [RTR-Netztest](https://www.netztest.at) on the web


License
-------

Copyright 2014-2017 Rundfunk und Telekom Regulierungs-GmbH (RTR-GmbH). This source code is licensed under the Apache license found in
the [LICENSE.txt](https://github.com/rtr-nettest/rmbtws/blob/master/LICENSE.txt) file.
The documentation to the project is licensed under the [CC BY-AT 3.0](https://creativecommons.org/licenses/by/3.0/at/deed.de_AT)
license.
