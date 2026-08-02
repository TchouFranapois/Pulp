\# PULP – Precompressed Upstream Layer Pipeline
copyright François Gauthier - Superwired-Labs


\[!\[License](https://img.shields.io/badge/License-AGPL%20v3%20%2F%20Commercial-blue.svg)](LICENSES/AGPL-3.0.txt)

\[!\[Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey)]()



\*\*PULP is a lightweight (50 KB) C library (DLL) that compresses, anonymises, and writes logs directly to disk at line-rate.\*\*

You link it into your own application and call its functions from your code. 
A companion command‑line utility (`PulpReader`) is provided to decode the binary archives back to text or JSON.



\*\*Key numbers (6‑core Ryzen 5 Pro 8640HS, NVMe SSD):\*\*

\- \*\*\ +21 million logs/second\*\* sustained throughput

\- \*\*\~3-6:1 compression\*\* on typical HTTP logs (semantic + LZ4)

\- \*\*As low as 20 MB memory\*\* footprint, fully deterministic 



\---



\## When to use PULP



| Your situation | What PULP brings |

|----------------|-------------------|

| You build a high‑traffic web server, proxy, or firewall on Windows and need to log millions of requests per second. | PULP compresses on the fly and writes directly to disk, keeping CPU and memory usage predictable. |

| You want to reduce the cost of log storage and network egress. | Lossless semantic pre‑compression plus LZ4 typically shrinks HTTP logs by a factor of 5. |

| You are legally required to anonymise IP addresses before storing logs. | AVX‑512 accelerated masking works on both IPv4 and IPv6, with configurable levels. |

| You need an audit‑proof, corruption‑resiliant binary archive for compliance. | PULP batches are self‑contained and can be decoded years later with the supplied command‑line tool. |

| You already have a log collector (Fluent Bit, Vector, etc.) and just want a faster writer. | PULP outputs compressed `.bin` files that any collector can ship; you decide when and how to move them. |

| You don't want to manage a separate logging service or daemon. | PULP is a single DLL, linked statically or dynamically into your own process. No extra process, no network ports, no heavy configuration. |



\---


## Why Windows?

Most ultra-high-performance telemetry tools (Vector, eBPF-based agents, Fluent Bit) are designed Linux-first. 
On Windows Server—which powers critical IIS infrastructure, high-throughput enterprise .NET apps, and low-latency C++ services—developers are often left with two undesirable options:

1. **Heavy managed logging frameworks** that cause Garbage Collection (GC) pauses and high CPU overhead under heavy traffic spikes.
2. **Cross-platform ports** that wrap Linux paradigms, losing performance through abstraction layers.

**PULP was built to solve this exact problem.**

Rather than using generic cross-platform wrappers, PULP leverages bare-metal Windows primitives:
- Direct **Thread-Local Storage (TLS)** for zero-contention hot path ingestion.
- Native **Windows Thread Pool API** for asynchronous compression tasks.
- Hardware-accelerated **AVX2 / SIMD intrinsics** tuned for x64 architecture.

By committing fully to the Win32 ecosystem, PULP delivers line-rate ingestion with zero GC impact and a deterministic memory footprint under.


\---


\## Features



\- \*\*Blazing fast\*\* – zero‑allocation hot path, per‑thread TLS caches, zero-eviction temporal cache, custom SIMD‑accelerated IP masking (AVX‑512).

\- \*\*On‑the‑fly compression\*\* – lossless semantic precompression paired with LZ4 reduces I/O volume before writing to disk.

\- \*\*Deterministic memory footprint\*\* – no memory spikes during activity surges; the architecture absorbs load seamlessly.

\- \*\*Dual‑mode IP anonymisation\*\* – configurable masking for IPv4 and IPv6, including support for CIDR, zones, and compressed addresses.

\- \*\*Inter-thread atomic numeration\*\* – easily reconstruct the absolute sequence order of millions of events/logs.

\- \*\*Automatic file rotation\*\* – smooth handle swapping and asynchronous deferred close eliminate write stalls.

\- \*\*Resilient disk handling\*\* – automatic fallback to a backup path on disk full, access denied, or path not found.

\- \*\*Resilient file format\*\* – Each log file is a sequence of independent blocks (batches), making the storage resilient to corruption

\- \*\*Rich telemetry\*\* – JSON statistics endpoint (cache hit ratios, compression ratios, throughput, backpressure, and more).

\- \*\*Dual licensing\*\* – AGPLv3 for open‑source use, or a commercial license for closed‑source products.



\---



\## Performance



Measured on a Lenovo ThinkPad P14s Gen 5 (Ryzen 5 Pro 8640HS, 96 GB RAM, NVMe SSD, Windows 11).

Workload: 1 000 unique URLs × 5 000 unique IPs, random distribution.


High-Performance Mode (6 caller threads):

250,000,000 logs in 11.52 seconds
21.71M logs/second sustained
~105MB RAM footprint
LZ4-only ratio: 1.5× (67% of original)
End-to-end ratio: ~5.16× (preprocessing + LZ4: 19.4% of the original)
0 logs lost, 0 backpressure events


Economy Mode (single caller thread):

250,000,000 logs in 29.52 seconds
8.47M logs/second
~16MB RAM footprint
LZ4-only ratio: 1.5× (67% of original)
End-to-end ratio: ~4.66× (preprocessing + LZ4: 21.5% of the original)
0 logs lost, 0 backpressure events



Performance depends on the hardware, the number of threads, the data entropy, cardinality, and the parameters passed to the initialisation function.




\## Requirements



\- Windows 10 / 11 or Windows Server 2016+ (x64).

\- CPU with \*\*AVX2\*\* support (all modern x86‑64 processors).

&#x20; \*\*AVX‑512 (F, BW, VL) is only required if IP anonymisation is enabled.\*\*

\- Visual Studio 2022 (solution provided).



\---



\## Quick Start



A complete working example is provided in the `LogProducer` project.
All values are 'per-thread'. All the caller threads use the same init values.
The DLL is intended to be called by a pool of (or one unique) long lived thread.



```c

\#include "pulp.h"



int main() {

&#x20;   // One‑time initialisation

uint8\_t rc = Pulp\_Init(

&#x20;       "C:\\\\Logs",               // primary log folder

&#x20;       "D:\\\\BackupLogs",         // backup folder (optional, can be "")

&#x20;       "C:\\\\Errors",             // error log folder

&#x20;       1,                        // enable/disable atomic inter‑thread sequence IDs

&#x20;       ANON\_IP\_2,                // enable/disable anonymization on last 4 octets (IPv4) / 4 hextets (IPv6)

&#x20;       1,                        // enable/disable strip URL query parameters (if URLs are to be logged)

&#x20;       128,                      // file rotation every 128 batches (must be power of 2)

&#x20;       COMPRESSION\_BALANCED,     // LZ4 compression level

&#x20;       BATCH\_8MB,                // 8MB active buffer or 262 144 in-flight logs

&#x20;       DICT\_256K                 // 256k slots cache

&#x20;   );



&#x20;   if (rc != RTN\_OK) {

&#x20;       // Handle initialisation error

&#x20;       return 1;

&#x20;   }



&#x20;   // Write a log/evt entry from any thread

uint16\_t res = PulpWrite(

&#x09;12,                                          // Numeric identifier of the operation (HTTP method, ICMP type, DNS opcode, etc.), user defined

&#x09;"https://www.resource/admin/overview.jpeg",  // Generic resource reference (URL, domain name, ICMP message, etc.) 563 char MAX, see API header for truncation politic.

&#x09;40,                                          // Length of the resource string without the terminating char.

&#x09;200,                                         // Generic response or error code (HTTP status, DNS RCODE, ICMP code/type, etc.)

&#x09;"172.21.22.23",                              // Target address (IPv4/IPv6, hostname, DNS server, etc.). Do NOT try IP\_ANON on non-IP endpoints.

&#x09;12,                                          // Length of the endpoint string

&#x09;4520,                                        // Duration in milliseconds (0–65535)

&#x09;8,                                           // Data size bucket (0 = 1–5 KB, 1 = 5–10 KB, etc.), user defined

&#x09;123,                                         // Free bitmask (bit 0 = encrypted, bit 1 = protocol version, bit 2 = fragmented, etc.), user defined

&#x09;1780008801123456                             // High-resolution timestamp in microseconds (UTC ISO-8601) ("2026-07-29T14:53:21.123456Z" as per the example)

);

&#x20;   

&#x20;   uint8\_t backpressure = res \& 0xFF;

&#x20;   uint8\_t error        = res >> 8;



&#x20;   // Graceful shutdown

&#x20;   Pulp\_Shutdown();

&#x20;   return 0;

}

```



\---



\## API Overview



The public API is declared in \*\*`pulp.h`\*\* and exported by `pulp.dll`.



| Function | Description |

|----------|-------------|

| `uint8\_t PulpInit(...)` | One‑time global initialisation. Configures paths, IP anonymisation, compression level, buffer/cache sizes and internal thread pools. Must be called before any other API function. |

| `uint16\_t PulpWrite(...)` | Hot‑path logging function. Accepts a URL, HTTP status, IP, timing, etc. Returns a packed status: high byte = system error, low byte = backpressure level. |

| `char\* PulpGetStats()` | Returns a JSON string containing live telemetry (throughput, cache hit ratios, compression ratio, queue depth, errors). The caller must free the string with `Pulp\_FreeStats()`. |

| `void PulpFreeStats(char\* p)` | Frees a statistics string previously obtained from `Pulp\_GetStats()`. |

| `void PulpShutdown()` | Graceful shutdown: flushes all pending data, waits for compression and writes to complete, releases all resources. |



For detailed parameter descriptions, see the API header `pulp.h`.



\---



\## Configuration Reference



\### Cache size (`DictSize`)



Number of unique URL/IP values the per‑thread cache can hold.



| Value | Slots | Recommended batch |

|-------|-------|-------------------|

| `DICT\_16K` | 16 384 | `BATCH\_500KB` |

| `DICT\_32K` | 32 768 | `BATCH\_1MB` |

| `DICT\_64K` | 65 536 | `BATCH\_2MB` |

| `DICT\_128K` | 131 072 | `BATCH\_4MB` |

| `DICT\_256K` | 262 144 | `BATCH\_8MB` |

| `DICT\_512K` | 524 288 | `BATCH\_16MB` |

| `DICT\_1M` | 1 048 576 | `BATCH\_32MB` |

| `DICT\_2M` – `DICT\_16M` | … | `BATCH\_64MB` – `BATCH\_512MB` |

| `DICT\_AUTOSIZE` | \~1/32 total RAM | auto |



\### Batch size (`BatchSize`)



Controls the flush threshold and the maximum in-flight logs (lost on a hard crash, power outage, etc.. The write queue can also hold batches waiting to be processed).



| Value | Max loss/thread |

|-------|-----------------|

| `BATCH\_500KB` | \~15 600 |

| `BATCH\_1MB` | \~32 768 |

| `BATCH\_4MB` | \~131 072 |

| `BATCH\_16MB` | \~524 288 |

| `BATCH\_512MB` | \~16 777 216 |

| `BATCH\_AUTOSIZE` | auto |



Flush is triggered by buffer pressure only (no timer). Call `Pulp\_Shutdown()` to flush remaining logs on shutdown.



\### Compression level (`Lz4CompressionLevel`)



Since compression isn't the bottleneck, COMPRESSION\_BALANCED is generally recommanded.



| Value | Description |

|-------|-------------|

| `COMPRESSION\_FAST` | LZ4 fastest mode |

| `COMPRESSION\_BALANCED` | LZ4 default – best ratio/speed tradeoff (recommended for most workloads) |

| `COMPRESSION\_NONE` | Pre‑compression only, no LZ4 – useful for debugging, more I/O pressure |



\### IP anonymisation (`Anon\_lvl`)



| Value | IPv4 result | IPv6 result |

|-------|-------------|-------------|

| `ANON\_IP\_NONE` | `192.168.2.23` | full address |

| `ANON\_IP\_1` | `192.168.2.x` | last 2 hextets masked |

| `ANON\_IP\_2` | `192.168.x.x` | last 4 hextets masked |

| `ANON\_IP\_3` | `192.x.x.x` | last 6 hextets masked |

| `ANON\_IP\_4` | `x.x.x.x` | full address masked |



> ⚠ Only use IP anonymisation on actual IP addresses. Do not apply it to hostnames or other non‑IP strings.



\---



\## Integration Suggestions



PULP is designed to be \*\*glued\*\* into your existing infrastructure. As a C native Library it can be interfaced with virtually everything. It writes compressed `.bin` files locally; you decide how to move and process them.

Hereafter are some implementations ideas, which would require adding only basic extensions to the project (2 \& 3) or are ready to use out of the box (1 \& 4).



\### 1. Optimally compressed local archiving for high traffic appliances



Just link PULP, call `Pulp\_Write()`, and the compressed shards accumulate in your log folder. Use `PulpReader` later to decode them.



\### 2. Centralised logging with a network share



All servers write to `\\\\nas\\logs\\`. A scheduled task on the central machine runs `PulpReader` against new files and pipes the output to your observability platform.



```powershell

\# Example PowerShell script, with json format and output filter extensions (not included in the sources, but easily implementable)

Get-ChildItem \\\\nas\\logs\\ -Filter \*.bin | ForEach-Object {

&#x20;   PulpReader.exe --input $\_.FullName --format json --filter "http\_code>=400" |

&#x20;       Invoke-RestMethod -Uri "\[https://api.datadog.com/v1/input](https://api.datadog.com/v1/input)" -Method Post

}

```



\### 3. Direct export via stdout (currently the PulpReader only output integral text)



```cmd

PulpReader.exe --input C:\\Logs\\shard\_12345.bin --format json --filter "http\_code>=500" |

&#x20;   curl -X POST \[https://api.datadog.com/v1/input](https://api.datadog.com/v1/input) -H "Content-Type: application/json" -d @-

```



\### 4. SIEM / cold storage decoding



```bash

PulpReader.exe E:\\Archives\\2025-01-01.bin E:\\Archives\\2025-01-01.txt

```



\---



## Economic Impact & Cost Savings

Logging infrastructure and cloud providers charge heavily for **network egress**
and **storage capacity**. By compressing logs directly at the source, PULP reduces
the data footprint by **2× to 5×** before it leaves your application process,
depending on log structure and entropy.

*The figures below are illustrative, based on a conservative 3× compression
factor. You can measure savings on your own workloads using the included
`LogProducer` benchmark project.*

| Raw Daily Log Volume | Typical Use Case | With PULP (3× footprint) | Estimated Annual Savings* |
| :--- | :--- | :--- | :--- |
| **50 GB / day** | SaaS Startup / SME | ~17 GB stored/shipped | **~$2,500 – $3,500** |
| **500 GB / day** | Mid‑Market / Growing Tech | ~167 GB stored/shipped | **~$25,000 – $35,000** |
| **5 TB / day** | Enterprise / High Traffic | ~1.7 TB stored/shipped | **~$250,000+** |

*\*Savings reflect bandwidth, local/cold storage, and self-hosted cluster capacity (Elastic, Loki).*  
*A lifetime commercial license typically pays for itself within weeks or days on high-volume nodes.*


\---



\## Architecture Overview



```

Pulp\_Write()  →  TLS context  →  URL/IP cache  →  active buffer

&#x20;                   ↑                                ↓ (buffer full)

&#x20;                   |                             Pulp\_Flush()

&#x20;                   |                                ↓

&#x20;                   |                             BuildDictionaryInMemory()

&#x20;                   |                                ↓

&#x20;                   |                            CompressionTask (thread pool)

&#x20;                   |                                ↓

&#x20;                   +——— WritePool\_Enqueue() → WriteThread → WriteFile()

&#x20;                                                            ↓

&#x20;                                                      RotationThread (file creation, handle swap)

```



Every thread owns its own cache and active buffer – no lock contention on the hot path. The write pool is a multi‑producer / multi‑consumer queue synchronised with slim reader‑writer locks and condition variables.



\---



\## File Format



Each log file is a sequence of independent blocks, making the storage resilient to corruption:
Each decompressed payload contains:

\- An array of `SerializedEntry` structs (32 bytes each)

\- A dictionary mapping cache indices back to their original string values (URLs and IPs)

Every block is independently decompressible. A partially written file can be read up to the last complete block.



\---



\## Live Telemetry



`Pulp\_GetStats()` returns a JSON snapshot (caller must free with `Pulp\_FreeStats()`):



```json

{

&#x20; "cache\_resource\_L1": 499999,

&#x20; "cache\_resource\_L1\_percent": 100.00,

&#x20; "log\_processed\_total": 500000,

&#x20; "batch\_flushed\_total": 32,

&#x20; "batch\_written\_total": 32,

&#x20; "backpressure\_count": 0,

&#x20; "compression\_ratio\_avg": 0.86,

&#x20; "lost\_logs\_total": 0,

&#x20; "log\_refused\_total": 0,

&#x20; "throughput\_sec": 6.55,

&#x20;  ...

}

```



> `Pulp\_GetStats()` introduces a \~2 s measurement window. Call it at most every few seconds with a dedicated thread.



\---



\## Limitations



\- \*\*Windows x64 only.\*\* The code uses AVX2/AVX‑512 intrinsics and Windows‑specific APIs (TLS, SRW locks, thread pools). A Linux port is planned.

\- \*\*No transactional durability for in‑flight data.\*\* Logs in the active buffer are lost on a hard crash. Loss is bounded to `BATCH\_SIZE / 32` entries per thread + the write queue if any (it's usually empty due to the speed of the write pool). Batches already flushed to disk are safe.

\- \*\*Flush triggered by throughput only\*\* – no background timer. Low‑traffic applications should use the smallest batch size (15 600 logs) probably with a unique long lived thread.

\- \*\*Fixed log schema.\*\* The `Pulp\_Write()` signature is optimised for HTTP structured logs. Arbitrary structured fields might require extending the source, although the current field semantic is fairly agnostic.



\---



\## Contributions & project philosophy

Contributors must adhere to the  Contributor Licence Agreement (CLA.md) located at the root of the project. 
Take time to read it before contributing.
The document also states the project's philosophy regarding the dual-license.

\---


\## Author


PULP was created by \*\*François Gauthier\*\* – Founder \& Software Architect, \[Superwired‑labs](https://www.linkedin.com/in/superwired-labs/).


\---



\## License



PULP is dual‑licensed:



\- \*\*Open Source\*\* – GNU Affero General Public License v3.0 (AGPLv3)

\- \*\*Commercial\*\* – a proprietary license for closed‑source products



Full license texts are in the `LICENSES/` folder.  

For commercial conditions \& pricing, contact \*\*fgauthier \[at] superwired-labs \[dot] com\*\*.



\### Third‑Party Libraries



| Library | Author | License |

|---------|--------|---------|

| \[CityHash](https://github.com/google/cityhash) | Google Inc. | MIT |

| \[xxHash](https://github.com/Cyan4973/xxHash) | Yann Collet | BSD‑2‑Clause |

| \[LZ4](https://github.com/lz4/lz4) | Yann Collet | BSD‑2‑Clause |



See `THIRD\_PARTY.md` for details.

