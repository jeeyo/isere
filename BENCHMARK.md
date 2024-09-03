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
    Latency     6.74ms   59.39ms   1.76s    98.70%
    Req/Sec   238.07    281.47     2.25k    86.21%
  18412 requests in 10.09s, 341.68KB read
  Socket errors: connect 0, read 155815, write 0, timeout 13
Requests/sec:   1824.04
Transfer/sec:     33.85KB
```

### isère on Raspberry Pi Pico 2

```
$ wrk -t12 -c400 -d10s http://192.168.7.1:8080
Running 10s test @ http://192.168.7.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    69.48ms  245.68ms   1.99s    95.52%
    Req/Sec    34.36     22.33    70.00     53.93%
  335 requests in 10.06s, 6.22KB read
  Socket errors: connect 0, read 1432, write 0, timeout 0
Requests/sec:     33.29
Transfer/sec:     632.42B
```
