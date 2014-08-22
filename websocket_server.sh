#! /bin/sh

# rmbtd init.d script for Debian like systems

### BEGIN INIT INFO
# Provides:          websocket_server
# Required-Start:    $local_fs $remote_fs $syslog $network $time
# Required-Stop:     $local_fs $remote_fs $syslog $network
# Should-Start:
# Should-Stop:
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Start/Stop the websocket_server daemon
### END INIT INFO

WORKDIR=/home/netztest/WebSocketServer
DAEMON=/home/netztest/netztest/RMBTServer/rmbtd
NAME=websocket_server
DESC=websocket_server

CERTFILE="netztest.crt"
KEYFILE="netztest.key"
LISTEN_SSL_PORTS="213.208.150.178:443 [2a01:190:1700:39::2:178]:443"
DAEMON_OPTS="-w -u netztest"
PIDFILE=/tmp/websocket_server.pid


test -x $DAEMON || exit 0

if ! [ -x "/lib/lsb/init-functions" ]; then
    . /lib/lsb/init-functions
else
    echo "E: /lib/lsb/init-functions not found, lsb-base (>= 3.0-6) needed"
    exit 1
fi

if [ -f /etc/default/websocket_server ] ; then
    . /etc/default/websocket_server
fi

for port in $LISTEN_SSL_PORTS ; do
    DAEMON_OPTS="$DAEMON_OPTS -L $port"
done

set -e

case "$1" in
  start)
        log_daemon_msg "Starting $DESC" "$NAME"
        start-stop-daemon --start -d $WORKDIR --pidfile $PIDFILE --exec "$DAEMON" -- -d -c $CERTFILE -k $KEYFILE $DAEMON_OPTS
        log_end_msg $?
        ;;
  stop)
        log_daemon_msg "Stopping $DESC" "$NAME"
        start-stop-daemon --stop --pidfile $PIDFILE --exec "$DAEMON" --quiet --oknodo --retry 15
        log_end_msg $?
        ;;
  status)
        status_of_proc -p $PIDFILE "$DAEMON" "$NAME" && exit 0 || exit $?
    ;;
  restart)
        $0 stop
        sleep 1
        $0 start
        ;;
  *)
        log_failure_msg "Usage: $N {start|stop|status|restart}" 
        exit 1
        ;;
esac

exit 0
