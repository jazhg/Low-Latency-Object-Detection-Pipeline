# Exploratory synthetic capacity sweep

Run on 2026-11-10 with 90 frames, 33 ms capture period, 100 ms fake service,
and display disabled. Each row is one run.

| Capacity | Mode | Processed | Overflow | Stale | FPS | p95 age (ms) |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 2 | FIFO | 31 | 59 | 0 | 9.707 | 307.3 |
| 2 | Latest | 30 | 31 | 29 | 9.663 | 171.0 |
| 4 | FIFO | 33 | 57 | 0 | 9.693 | 512.0 |
| 4 | Latest | 30 | 0 | 60 | 9.693 | 134.5 |
| 8 | FIFO | 37 | 53 | 0 | 9.681 | 925.4 |
| 8 | Latest | 30 | 0 | 60 | 9.706 | 134.7 |
| 16 | FIFO | 45 | 45 | 0 | 9.714 | 1741.4 |
| 16 | Latest | 30 | 0 | 60 | 9.673 | 133.4 |
| 32 | FIFO | 61 | 29 | 0 | 9.702 | 3385.7 |
| 32 | Latest | 30 | 0 | 60 | 9.650 | 135.1 |

At capacity 8, latest reduced p95 age by 85.4% while throughput differed by
0.25%. Capacity 2 demonstrates the design limit: the queue fills during service,
so latest mode rejects newer frames and p95 age rises.
