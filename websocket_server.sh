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
# daemon is a symlink to /home/netztest/netztest/RMBTServer/rmbtd
DAEMON=/home/netztest/WebSocketServer/websocket_server
NAME=websocket_server
DESC=websocket_server
# you need recognised certificates here
CERTFILE="netztest.crt"
KEYFILE="netztest.key"
# replace these addresses with your public IPs
LISTEN_SSL_PORTS="192.168.1.2:443 [fd93:4f3e:8299::2]:443"
DAEMON_OPTS="-w -u netztest"
PIDFILE=/var/run/netztest/websocket_server.pid


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
        start-stop-daemon --start -d $WORKDIR --exec "$DAEMON" -- -d -c $CERTFILE -k $KEYFILE $DAEMON_OPTS
        log_end_msg $?
        ;;
  stop)
        log_daemon_msg "Stopping $DESC" "$NAME"
        start-stop-daemon --stop --exec "$DAEMON" --quiet --oknodo --retry 15
        log_end_msg $?
        ;;
  status)
        status_of_proc "$DAEMON" "$NAME" && exit 0 || exit $?
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
