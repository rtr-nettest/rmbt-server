RMBT Test Server
================

> This project contains the RMBT Test Server for conducting measurements based on 
  the RMBT protocol. Clients can communicate either directly via TCP sockets or based on 
  the WebSocket protocol.

Usage
-----

Generate the random file, your secret key and the server executable using

```
> make random
> make key
> make all
```

The server uses keys to authenticate clients, as specified in the RMBT protocol. These keys that 
are loaded from the `secret.key` file at startup. Each line in this file contains the key as well 
as a space-separated label, which will be sent to the syslog each time a client identifies 
using this key.

For running the server, either use the supplied init.d scripts or the ```rmbtd``` executable.

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

 -w     use as websocket server (no argument)
 
 -v     behave as version (v) for serving older clients
        example: "0.3"

```

Related materials
-----------------

* [RMBT specification](https://www.netztest.at/doc/)
* [RMBTws Client](https://github.com/rtr-nettest/rmbtws)
* [RTR-Netztest/open-rmbt](https://github.com/rtr-nettest/open-rmbt)
  

Get in Touch
------------

* [RTR-Netztest](https://www.netztest.at) on the web


License
-------

Copyright 2014-2017 Rundfunk und Telekom Regulierungs-GmbH (RTR-GmbH). This source code is licensed under the Apache license found in
the [LICENSE.txt](https://github.com/rtr-nettest/rmbtws/blob/master/LICENSE.txt) file.
The documentation to the project is licensed under the [CC BY-AT 3.0](https://creativecommons.org/licenses/by/3.0/at/deed.de_AT)
license.
