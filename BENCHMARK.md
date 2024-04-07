# Benchmark

### Node 20

```
$ wrk -t12 -c400 -d10s http://127.0.0.1:8080
Running 10s test @ http://127.0.0.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     3.13ms    6.22ms 249.40ms   99.33%
    Req/Sec     7.19k     3.31k   13.33k    58.25%
  860006 requests in 10.02s, 141.07MB read
  Socket errors: connect 155, read 320, write 0, timeout 0
Requests/sec:  85861.04
Transfer/sec:     14.08MB
```

### isère

##### single-threaded with event loop

```
$ wrk -t12 -c400 -d10s http://127.0.0.1:8080
Running 10s test @ http://127.0.0.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    11.81ms   82.85ms 817.42ms   98.86%
    Req/Sec    76.03    153.36     1.01k    93.00%
  877 requests in 10.09s, 34.26KB read
  Socket errors: connect 155, read 17021, write 0, timeout 0
Requests/sec:     86.88
Transfer/sec:      3.39KB
```
