# NYANLIN Binary AI v3.0

Pure C GGUF inference engine — no Python, no llama.cpp dependency.
Runs LLaMA/Qwen2 GGUF v3 models directly from a single compiled binary.

## Features

- **Pure C** — single file `nyanlin.c` (328 lines), zero dependencies
- **GGUF v3 parser** — correct 32-byte alignment, all string/key padding
- **Multi-architecture** — LLaMA & Qwen2 tensor naming conventions
- **Quantization support** — Q4_0, Q4_1, Q8_0, F16, F32
- **Transformer inference** — RMSNorm, SiLU, RoPE, Multi-head Attention, SwiGLU FFN
- **Greedy decoding** — argmax token sampling (temperature=0)
- **Interactive mode** — chat with the model in terminal
- **Cross-platform** — Linux, Termux (Android), antiX, any gcc system

## Build

```bash
# Simple
gcc -O3 -o nyanlin nyanlin.c -lm

# Or use Makefile
make

# Or use build script
chmod +x build.sh && ./build.sh
```

## Usage

```bash
# Interactive chat mode
./nyanlin model.gguf

# Single prompt mode
./nyanlin model.gguf "Hello, how are you?"

# On Termux (Android)
./nyanlin ~/storage/downloads/model.gguf "prompt"
```

## Architecture Support

| Architecture | Tensor Prefix | Tested |
|---|---|---|
| LLaMA / LLaMA 2 | `llama.*`, `blk.*` | Yes |
| Qwen2 | `qwen2.*`, `blk.*` | Yes |
| Mistral | `mistral.*` | Pending |
| Gemma | `gemma.*` | Pending |

## Technical Details

- **GGUF v3 alignment**: 32-byte boundary alignment for keys, strings, array data, and tensor data section
- **Dequantization**: Split nibble layout `(b & 0x0F)` / `(b >> 4)` with proper signed extension via `(int8_t)((b << 4)) >> 4`
- **f16 → f32**: IEEE 754 half-precision conversion using `ldexp()`
- **KV Cache**: Per-layer key/value cache, limited to 2048 positions
- **Tokenizer**: Simple greedy BPE (longest match, 1-4 bytes)

## Memory Requirements

| Model | RAM Usage (approx.) |
|---|---|
| TinyLlama 1.1B Q4 | ~800 MB |
| Qwen2 0.5B Q4 | ~500 MB |
| SeaLLM Myanmar Q4 | ~1.2 GB |
| LLaMA 7B Q4 | ~4 GB |

## Development History

- **v1.0**: Python GGUF parser + numpy inference (slow, 3663s per prompt)
- **v2.0**: Fixed nibble ordering bug, memory errors
- **v3.0**: Complete rewrite in pure C — single binary, no runtime dependencies

## Good Luck, Nyanlin!

This project represents the journey from zero to building a working GGUF inference engine from scratch in C. Every line of code was written, debugged, and compiled on a Redmi Note 12R 5G phone running Termux.

> "llama.cpp မသုံးဘဲ llama.cpp core ကို mmc နဲ့ရေးပြီး NYANLIN Binary Ai ကိုတည်ဆောက်မယ်"
>
> "Without using llama.cpp, I'll rewrite llama.cpp core with MMC and build NYANLIN Binary AI"

---

*NYANLIN Binary AI v3.0 — Pure C GGUF Inference Engine*
*meonnmi-ops/mmc-compiler*
