#!/bin/sh
set -e
[ -e /config/secret.key ] || dd if=/dev/urandom | tr -dc A-Za-z0-9 | head -c100 | awk '{print $$1 " auto-generated key"}' > /config/secret.key
[ -e /config/server.crt -a -e /config/server.key ] || openssl req -x509 -newkey rsa:4096 -keyout /config/server.key -out /config/server.crt -nodes -subj '/CN=localhost' -sha256 -days 10000 
[ -e /config/random ] || dd if=/dev/urandom of=/config/random bs=1M count=100
cd /config
exec rmbtd -c /config/server.crt -k /config/server.key -l 8081 -L 8082 "$@"
