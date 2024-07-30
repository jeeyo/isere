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
    Latency     1.73ms  558.36us   8.59ms   69.06%
    Req/Sec   558.37    494.71     1.90k    63.71%
  6987 requests in 10.09s, 129.64KB read
  Socket errors: connect 155, read 13065, write 12, timeout 0
Requests/sec:    692.35
Transfer/sec:     12.85KB
```

##### with QuickJS

```
$ wrk -t12 -c400 -d10s http://127.0.0.1:8080
Running 10s test @ http://127.0.0.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    20.83ms   39.12ms 486.87ms   96.55%
    Req/Sec    38.17     52.56   230.00     85.32%
  551 requests in 10.09s, 18.83KB read
  Socket errors: connect 155, read 2111, write 12, timeout 0
Requests/sec:     54.59
Transfer/sec:      1.87KB
```
