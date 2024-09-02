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

### isère on Apple M1

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

### isère on Raspberry Pi Pico 2

```
$ wrk -t12 -c400 -d10s http://192.168.7.1:8080
Running 10s test @ http://192.168.7.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.19s   239.32ms   1.88s    72.73%
    Req/Sec     0.73      0.83     3.00     86.36%
  22 requests in 10.08s, 770.00B read
  Socket errors: connect 0, read 1321, write 0, timeout 0
Requests/sec:      2.18
Transfer/sec:      76.40B
```
