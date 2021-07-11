

# profiler

```bash
sudo apt install libgoogle-perftools-dev google-perftools
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libprofiler.so CPUPROFILE=out.prof ./bin/http_benchmark_server
google-pprof --pdf ./bin/http_benchmark_server out.prof > out.pdf
```
