GCC_PARAMS = -Wall -Wno-unused-value -o rmbtd rmbtd.c -pthread -lrt -lssl -lcrypto
SERVER_DEP = rmbtd.c config.h secret.h

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
