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

```
$ wrk -t12 -c400 -d10s http://127.0.0.1:8080
Running 10s test @ http://127.0.0.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   112.54ms  240.50ms   1.85s    95.20%
    Req/Sec    25.98     21.73   151.00     75.93%
  1346 requests in 10.03s, 57.88KB read
  Socket errors: connect 0, read 1346, write 0, timeout 12
Requests/sec:    134.14
Transfer/sec:      5.77KB
```

### isère on Raspberry Pi Pico 2

```
$ wrk -t12 -c400 -d10s http://192.168.7.1:8080
Running 10s test @ http://192.168.7.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   314.92ms  194.94ms   1.09s    93.02%
    Req/Sec     3.93      3.61    10.00     56.98%
  86 requests in 10.08s, 3.70KB read
  Socket errors: connect 0, read 4009, write 0, timeout 0
Requests/sec:      8.53
Transfer/sec:     375.30B
```
