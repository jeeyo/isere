# Benchmark

Tested with WSL2 on Intel(R) Core(TM) i5-9400F CPU @ 2.90GHz with 16GB RAM

### Node 20

```bash
$ wrk -t12 -c400 -d10s http://127.0.0.1:8080
Running 10s test @ http://127.0.0.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    42.32ms  149.54ms   1.99s    96.70%
    Req/Sec     1.71k   648.15     7.07k    85.64%
  190318 requests in 10.07s, 23.78MB read
  Socket errors: connect 0, read 0, write 0, timeout 134
Requests/sec:  18902.95
Transfer/sec:      2.36MB
```

### isère

```bash
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

```bash
$ wrk -t12 -c400 -d10s http://192.168.7.1:8080
Running 10s test @ http://192.168.7.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   232.75ms  110.35ms 825.11ms   90.43%
    Req/Sec     3.95      3.32    10.00     65.22%
  115 requests in 10.05s, 3.93KB read
  Socket errors: connect 0, read 4226, write 0, timeout 0
Requests/sec:     11.44
Transfer/sec:     400.46B
```
