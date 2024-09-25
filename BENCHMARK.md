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
    Latency    57.24ms  186.63ms   1.81s    95.36%
    Req/Sec    64.04     52.16   310.00     77.97%
  3027 requests in 10.06s, 103.46KB read
  Socket errors: connect 0, read 3027, write 0, timeout 9
Requests/sec:    300.86
Transfer/sec:     10.28KB
```

### isère on Raspberry Pi Pico 2

```bash
$ wrk -t12 -c400 -d10s http://192.168.7.1:8080
Running 10s test @ http://192.168.7.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   218.71ms  128.51ms 817.69ms   82.91%
    Req/Sec     4.57      4.25    20.00     53.98%
  117 requests in 10.05s, 4.03KB read
  Socket errors: connect 0, read 4071, write 0, timeout 0
Requests/sec:     11.64
Transfer/sec:     410.87B
```
