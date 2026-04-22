# NYANLIN AI - Pure Python GGUF Inference Engine

![Python](https://img.shields.io/badge/Python-3.8+-blue)
![License](https://img.shields.io/badge/License-MIT-green)

NYANLIN (Nyan Lin = နှစ်လုံး in Myanmar) is a pure Python AI inference engine that can run GGUF format models without PyTorch or llama.cpp. Just numpy and Python!

## Features

- **Pure Python** - No PyTorch, no C++ bindings, no llama.cpp. Just Python + numpy.
- **GGUF v3** - Full GGUF format parser with metadata and tensor support
- **Multiple Architectures** - Qwen2, Llama, Mistral, Gemma2
- **Multiple Quantizations** - F16, BF16, Q8_0, Q8_1, Q4_0, Q4_1, Q5_K_M
- **BPE Tokenizer** - SentencePiece compatible with O(1) lookup
- **Sampling** - Temperature, top-k, top-p, repeat penalty
- **Interactive Chat** - Built-in chat mode with Qwen2 template
- **Low Memory** - Runs on 4GB RAM systems

## Requirements

- Python 3.8+
- numpy (`pip install numpy`)

## Quick Start

### 1. Clone and setup

```bash
git clone https://github.com/meonnmi-ops/mmc-compiler.git
cd mmc-compiler/nyanlin

# Create virtual environment (recommended)
python3 -m venv nyanlin-env
source nyanlin-env/bin/activate   # Linux/Mac
# nyanlin-env\Scripts\activate    # Windows

# Install dependency
pip install numpy
```

### 2. Download a model

For 4GB RAM systems, use a small model:

```bash
# Qwen2.5 0.5B Instruct (F16, ~1.3GB)
pip install huggingface_hub
python3 -c "
from huggingface_hub import hf_hub_download
hf_hub_download('Qwen/Qwen2.5-0.5B-Instruct-GGUF',
    'qwen2.5-0.5b-instruct-fp16.gguf',
    local_dir='./models')
"
```

### 3. Run!

```bash
# Interactive chat
python main.py ./models/qwen2.5-0.5b-instruct-fp16.gguf

# Single prompt
python main.py ./models/qwen2.5-0.5b-instruct-fp16.gguf --prompt "Hello!"

# With parameters
python main.py ./models/qwen2.5-0.5b-instruct-fp16.gguf \
    --prompt "What is Python?" \
    --max-tokens 128 \
    --temperature 0.7
```

## Command Line Options

| Option | Default | Description |
|--------|---------|-------------|
| `--prompt "text"` | (chat mode) | Input prompt text |
| `--max-tokens N` | 256 | Max tokens to generate |
| `--temperature F` | 0.8 | Sampling temperature (0.1-2.0) |
| `--top-k N` | 40 | Top-K filtering |
| `--top-p F` | 0.95 | Nucleus sampling threshold |
| `--repeat-penalty F` | 1.1 | Repeat penalty (1.0 = disabled) |

## Performance Tips

- **4GB RAM**: Use 0.5B models with F16 or Q8_0 quantization
- **8GB RAM**: Can handle 1.5B-3B models with Q4 quantization
- **First run is slow**: Prefill phase processes all input tokens. Subsequent tokens are faster.
- **Use virtual environment**: Avoid system Python conflicts

## Project Structure

```
nyanlin/
  __init__.py
  main.py                    # Entry point
  core/
    __init__.py
    gguf_parser.py           # GGUF v3 file parser + dequantization
    model_loader.py          # Model config from metadata
    tokenizer.py             # BPE tokenizer (SentencePiece)
    tensor.py                # Tensor wrapper
  layers/
    __init__.py
    attention.py             # Multi-head attention with GQA + RoPE
    feedforward.py           # SwiGLU feed-forward network
  inference/
    __init__.py
    sampler.py               # Temperature, top-k, top-p sampling
    generator.py             # Full inference pipeline
```

## License

MIT License
