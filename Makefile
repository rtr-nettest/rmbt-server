GCC_PARAMS = -Wall -Wno-unused-value -o rmbtd cwebsocket/base64_enc.c cwebsocket/sha1.c cwebsocket/websocket.c rmbtd.c -pthread -lrt -lssl -lcrypto
SERVER_DEP = cwebsocket/websocket.c rmbtd.c config.h secret.h

all: rmbtd

rmbtd: ${SERVER_DEP}
	gcc -O0 -g ${GCC_PARAMS}

server-prod: ${SERVER_DEP}
	gcc -O3 ${GCC_PARAMS}
	
clean:
	rm rmbtd

run: random rmbtd
	./rmbtd
	
random: 
	dd if=/dev/urandom of=random bs=1M count=100
