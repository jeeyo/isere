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
    Latency    27.80ms   58.80ms 988.08ms   97.72%
    Req/Sec    62.69     53.59   210.00     61.07%
  876 requests in 10.09s, 29.94KB read
  Socket errors: connect 155, read 2605, write 4, timeout 0
Requests/sec:     86.84
Transfer/sec:      2.97KB
```
