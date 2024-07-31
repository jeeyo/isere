FROM alpine AS builder

RUN apk add build-base git make cmake xxd

WORKDIR /app
COPY . .

RUN mkdir build && cd build && cmake .. && make -j

# FROM gcr.io/distroless/cc
FROM alpine

WORKDIR /app
COPY --from=builder /app/build/isere /app/isere
COPY --from=builder /app/build/handler.so /app/handler.so

ENTRYPOINT [ "/app/isere" ]
