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
    Latency     1.19s   239.32ms   1.88s    72.73%
    Req/Sec     0.73      0.83     3.00     86.36%
  22 requests in 10.08s, 770.00B read
  Socket errors: connect 0, read 1321, write 0, timeout 0
Requests/sec:      2.18
Transfer/sec:      76.40B
```
