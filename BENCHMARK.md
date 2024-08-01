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

##### without QuickJS

```
$ wrk -t12 -c400 -d10s http://127.0.0.1:8080
Running 10s test @ http://127.0.0.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     2.73ms   20.49ms 439.61ms   99.78%
    Req/Sec   428.20    429.25     1.60k    80.27%
  6367 requests in 10.09s, 118.14KB read
  Socket errors: connect 155, read 12230, write 21, timeout 0
Requests/sec:    631.03
Transfer/sec:     11.71KB
```

##### with QuickJS

```
$ wrk -t12 -c400 -d10s http://127.0.0.1:8080
Running 10s test @ http://127.0.0.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    99.14ms   49.36ms 363.49ms   84.35%
    Req/Sec    22.88     18.13    90.00     74.92%
  786 requests in 10.10s, 53.73KB read
  Socket errors: connect 155, read 2163, write 15, timeout 0
Requests/sec:     77.85
Transfer/sec:      5.32KB
```
