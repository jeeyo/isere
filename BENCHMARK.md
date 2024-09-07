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
    Latency    63.75ms   18.29ms 309.20ms   85.25%
    Req/Sec    28.73     19.29   150.00     75.18%
  2013 requests in 10.07s, 86.50KB read
  Socket errors: connect 0, read 41121, write 0, timeout 0
Requests/sec:    199.94
Transfer/sec:      8.59KB
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
