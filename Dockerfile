FROM gcc:12.2 as builder

WORKDIR /app
COPY . .

RUN make -j

WORKDIR /app/examples
RUN make -j

# FROM gcr.io/distroless/cc
FROM alpine

WORKDIR /app
COPY --from=builder /app/isere /app/isere
COPY --from=builder /app/examples/*.so /app/examples/

ENTRYPOINT [ "/app/isere" ]
