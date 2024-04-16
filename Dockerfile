FROM gcc:12.2 as builder

WORKDIR /app
COPY . .

RUN apt-get update && \
    apt-get install -y build-essential automake libtool cmake

RUN mkdir build && cd build && cmake .. && make -j

# FROM gcr.io/distroless/cc
FROM alpine

WORKDIR /app
COPY --from=builder /app/build/isere /app/isere
COPY --from=builder /app/build/handler.so /app/handler.so

ENTRYPOINT [ "/app/isere" ]
