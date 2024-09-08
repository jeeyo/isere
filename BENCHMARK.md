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
    Latency    83.71ms  202.49ms   1.80s    94.84%
    Req/Sec    39.83     33.50   190.00     81.59%
  1837 requests in 10.06s, 62.79KB read
  Socket errors: connect 0, read 1837, write 0, timeout 16
Requests/sec:    182.62
Transfer/sec:      6.24KB
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
