#####
FROM alpine AS build-env
RUN apk add --no-cache util-linux-dev gcc autoconf automake make openssl-dev musl-dev

COPY . /work
WORKDIR /work

RUN sed -i 's|#define CHECK_TOKEN 1|#define CHECK_TOKEN 0|' config.h
RUN make server-prod

#####
FROM alpine
ARG GIT_VERSION
LABEL version=${GIT_VERSION:-unknown}
RUN apk add --no-cache libuuid json-c openssl
COPY --from=build-env /work/rmbtd /bin/rmbtd
COPY docker-entrypoint.sh /
VOLUME /config
EXPOSE 8081 8082
ENTRYPOINT ["/docker-entrypoint.sh"]
