"""
NYANLIN Generator - Full inference pipeline

Architecture: GGUF -> weights -> token-by-token generation

Supports:
  - Qwen2 (model.layers.{i}.self_attn.q_proj.weight)
  - Llama  (model.layers.{i}.self_attn.q_proj.weight)
  - Mistral (model.layers.{i}.self_attn.q_proj.weight)
  - Gemma2  (model.layers.{i}.self_attn.q_proj.weight)

All use the HuggingFace naming convention. Falls back to GGUF naming for older models.
"""

import numpy as np
import time
import sys

from ..core.gguf_parser import GGUFReader
from ..core.model_loader import ModelLoader
from ..core.tokenizer import BPETokenizer
from ..layers.attention import Attention
from ..layers.feedforward import FeedForward
from .sampler import Sampler


# Tensor name mapping per architecture
TENSOR_NAME_MAP = {
    "qwen2": {
        "token_emb": "model.embed_tokens.weight",
        "output_norm": "model.norm.weight",
        "output_head": "lm_head.weight",
        "attn_norm": "model.layers.{i}.input_layernorm.weight",
        "ffn_norm": "model.layers.{i}.post_attention_layernorm.weight",
        "wq": "model.layers.{i}.self_attn.q_proj.weight",
        "wk": "model.layers.{i}.self_attn.k_proj.weight",
        "wv": "model.layers.{i}.self_attn.v_proj.weight",
        "wo": "model.layers.{i}.self_attn.o_proj.weight",
        "w_gate": "model.layers.{i}.mlp.gate_proj.weight",
        "w_up": "model.layers.{i}.mlp.up_proj.weight",
        "w_down": "model.layers.{i}.mlp.down_proj.weight",
    },
    "llama": {
        "token_emb": "model.embed_tokens.weight",
        "output_norm": "model.norm.weight",
        "output_head": "lm_head.weight",
        "attn_norm": "model.layers.{i}.input_layernorm.weight",
        "ffn_norm": "model.layers.{i}.post_attention_layernorm.weight",
        "wq": "model.layers.{i}.self_attn.q_proj.weight",
        "wk": "model.layers.{i}.self_attn.k_proj.weight",
        "wv": "model.layers.{i}.self_attn.v_proj.weight",
        "wo": "model.layers.{i}.self_attn.o_proj.weight",
        "w_gate": "model.layers.{i}.mlp.gate_proj.weight",
        "w_up": "model.layers.{i}.mlp.up_proj.weight",
        "w_down": "model.layers.{i}.mlp.down_proj.weight",
    },
    "mistral": {
        "token_emb": "model.embed_tokens.weight",
        "output_norm": "model.norm.weight",
        "output_head": "lm_head.weight",
        "attn_norm": "model.layers.{i}.input_layernorm.weight",
        "ffn_norm": "model.layers.{i}.post_attention_layernorm.weight",
        "wq": "model.layers.{i}.self_attn.q_proj.weight",
        "wk": "model.layers.{i}.self_attn.k_proj.weight",
        "wv": "model.layers.{i}.self_attn.v_proj.weight",
        "wo": "model.layers.{i}.self_attn.o_proj.weight",
        "w_gate": "model.layers.{i}.mlp.gate_proj.weight",
        "w_up": "model.layers.{i}.mlp.up_proj.weight",
        "w_down": "model.layers.{i}.mlp.down_proj.weight",
    },
    "gemma2": {
        "token_emb": "model.embed_tokens.weight",
        "output_norm": "model.norm.weight",
        "output_head": "lm_head.weight",
        "attn_norm": "model.layers.{i}.input_layernorm.weight",
        "ffn_norm": "model.layers.{i}.post_attention_layernorm.weight",
        "wq": "model.layers.{i}.self_attn.q_proj.weight",
        "wk": "model.layers.{i}.self_attn.k_proj.weight",
        "wv": "model.layers.{i}.self_attn.v_proj.weight",
        "wo": "model.layers.{i}.self_attn.o_proj.weight",
        "w_gate": "model.layers.{i}.mlp.gate_proj.weight",
        "w_up": "model.layers.{i}.mlp.up_proj.weight",
        "w_down": "model.layers.{i}.mlp.down_proj.weight",
    },
    # Fallback for older GGUF files with 'blk.' naming
    "gguf_old": {
        "token_emb": "token_embd.weight",
        "output_norm": "output_norm.weight",
        "output_head": "output.weight",
        "attn_norm": "blk.{i}.attn_norm.weight",
        "ffn_norm": "blk.{i}.ffn_norm.weight",
        "wq": "blk.{i}.attn_q.weight",
        "wk": "blk.{i}.attn_k.weight",
        "wv": "blk.{i}.attn_v.weight",
        "wo": "blk.{i}.attn_output.weight",
        "w_gate": "blk.{i}.ffn_gate.weight",
        "w_up": "blk.{i}.ffn_up.weight",
        "w_down": "blk.{i}.ffn_down.weight",
    },
}


class Generator:
    """NYANLIN Inference Generator - loads GGUF model and generates text."""

    def __init__(self, model_path=None, tokenizer_path=None):
        self.model_path = model_path
        self.tokenizer_path = tokenizer_path

        self.gguf = None
        self.model_loader = None
        self.tokenizer = None
        self.sampler = None

        self.config = {}
        self.layers = []
        self.token_emb = None
        self.output_norm = None
        self.output_head = None
        self.tensor_name_map = None

        self._built = False
        self._loaded = False

    def build(self, model_path=None):
        """Load GGUF file, parse metadata, build model structure.

        This step is fast - only parses headers and metadata.
        Weight loading happens in load().
        """
        if model_path:
            self.model_path = model_path
        if not self.model_path:
            raise ValueError("No model path provided")

        print(f"\n{'='*50}")
        print(f"NYANLIN AI Inference Engine v2.0")
        print(f"{'='*50}")

        # Open GGUF file
        print(f"\n[1/4] Parsing GGUF file: {self.model_path}")
        self.gguf = GGUFReader(self.model_path)

        # Load model config
        print(f"\n[2/4] Loading model configuration...")
        self.model_loader = ModelLoader(self.gguf)
        self.config = self.model_loader.config
        self.arch = self.config.get("arch", "unknown")

        # Select tensor name map
        if self.arch in TENSOR_NAME_MAP:
            self.tensor_name_map = TENSOR_NAME_MAP[self.arch]
        else:
            print(f"[Generator] Unknown arch '{self.arch}', using old GGUF naming")
            self.tensor_name_map = TENSOR_NAME_MAP["gguf_old"]

        # Load tokenizer
        print(f"\n[3/4] Loading tokenizer...")
        self.tokenizer = BPETokenizer(self.gguf)

        # Create sampler with defaults
        self.sampler = Sampler(temperature=0.8, top_k=40, top_p=0.95)

        # Build layer structures (without weights yet)
        print(f"\n[4/4] Building {self.config['block_count']} transformer layers...")
        self._build_layers()

        self._built = True
        print(f"\n{'='*50}")
        print(f"Build complete! Call load() to load weights.")
        print(f"{'='*50}")

    def _build_layers(self):
        """Create attention and feed-forward layer objects."""
        self.layers = []
        dim = self.config["embedding_length"]
        n_heads = self.config["head_count"]
        n_kv_heads = self.config["head_count_kv"]
        ff_dim = self.config["feed_forward_length"]
        eps = self.config.get("attention_layer_norm_rms_epsilon", 1e-5)

        head_dim = dim // n_heads
        self.head_dim = head_dim

        for i in range(self.config["block_count"]):
            attn = Attention(dim=dim, n_heads=n_heads, n_kv_heads=n_kv_heads,
                           head_dim=head_dim, eps=eps)
            ffn = FeedForward(dim=dim, intermediate_dim=ff_dim, eps=eps)
            self.layers.append({"attn": attn, "ffn": ffn})

        print(f"  Created {len(self.layers)} layers: "
              f"dim={dim}, heads={n_heads}, kv_heads={n_kv_heads}, "
              f"ff_dim={ff_dim}, head_dim={head_dim}")

    def load(self):
        """Load all weights from GGUF file. This is the slow step."""
        if not self._built:
            raise RuntimeError("Call build() first!")

        print(f"\n[Loading weights from GGUF...]")
        print(f"  This may take a while for large models...")
        t0 = time.time()

        # Build tensor lookup dict: name -> tensor_info
        tensor_lookup = {}
        for ti in self.gguf.tensor_infos:
            tensor_lookup[ti["name"]] = ti

        nmap = self.tensor_name_map

        # Load token embeddings
        emb_name = nmap["token_emb"]
        if emb_name in tensor_lookup:
            self.token_emb = self.gguf.read_tensor_as_f32(tensor_lookup[emb_name])
            print(f"  [OK] Token embedding: {self.token_emb.shape}")
        else:
            raise ValueError(f"Token embedding not found: '{emb_name}'")

        # Load output norm
        norm_name = nmap["output_norm"]
        if norm_name in tensor_lookup:
            self.output_norm = self.gguf.read_tensor_as_f32(tensor_lookup[norm_name])
            print(f"  [OK] Output norm: {self.output_norm.shape}")
        else:
            raise ValueError(f"Output norm not found: '{norm_name}'")

        # Load output head (lm_head)
        head_name = nmap["output_head"]
        if head_name in tensor_lookup:
            self.output_head = self.gguf.read_tensor_as_f32(tensor_lookup[head_name])
            print(f"  [OK] Output head: {self.output_head.shape}")
        else:
            raise ValueError(f"Output head not found: '{head_name}'")

        # Load per-layer weights
        n_layers = self.config["block_count"]
        for i in range(n_layers):
            layer = self.layers[i]
            layer_prefix = f"  Layer {i+1}/{n_layers}"

            # Attention weights
            wq_name = nmap["wq"].format(i=i)
            wk_name = nmap["wk"].format(i=i)
            wv_name = nmap["wv"].format(i=i)
            wo_name = nmap["wo"].format(i=i)
            attn_norm_name = nmap["attn_norm"].format(i=i)

            # FFN weights
            w_gate_name = nmap["w_gate"].format(i=i)
            w_up_name = nmap["w_up"].format(i=i)
            w_down_name = nmap["w_down"].format(i=i)
            ffn_norm_name = nmap["ffn_norm"].format(i=i)

            # Check all weights exist
            missing = []
            for name in [wq_name, wk_name, wv_name, wo_name, attn_norm_name,
                        w_gate_name, w_up_name, w_down_name, ffn_norm_name]:
                if name not in tensor_lookup:
                    missing.append(name)

            if missing:
                print(f"  {layer_prefix} MISSING: {missing[:3]}{'...' if len(missing) > 3 else ''}")
                # Try to continue with missing weights (some may be optional)
                continue

            attn = layer["attn"]
            ffn = layer["ffn"]

            attn.wq = self.gguf.read_tensor_as_f32(tensor_lookup[wq_name])
            attn.wk = self.gguf.read_tensor_as_f32(tensor_lookup[wk_name])
            attn.wv = self.gguf.read_tensor_as_f32(tensor_lookup[wv_name])
            attn.wo = self.gguf.read_tensor_as_f32(tensor_lookup[wo_name])
            attn.w_norm = self.gguf.read_tensor_as_f32(tensor_lookup[attn_norm_name])

            ffn.w_gate = self.gguf.read_tensor_as_f32(tensor_lookup[w_gate_name])
            ffn.w_up = self.gguf.read_tensor_as_f32(tensor_lookup[w_up_name])
            ffn.w_down = self.gguf.read_tensor_as_f32(tensor_lookup[w_down_name])
            ffn.w_norm = self.gguf.read_tensor_as_f32(tensor_lookup[ffn_norm_name])

            if (i + 1) % 4 == 0 or i == n_layers - 1:
                elapsed = time.time() - t0
                print(f"  {layer_prefix} loaded [{elapsed:.1f}s]")

        elapsed = time.time() - t0
        print(f"\n  All weights loaded in {elapsed:.1f}s")
        self._loaded = True

    def _forward_token(self, token_id, position):
        """Run forward pass for a single token.

        Returns: logits array [vocab_size]
        """
        # Token embedding
        x = self.token_emb[token_id].astype(np.float32)  # [dim]

        # Through all transformer layers
        for layer in self.layers:
            attn = layer["attn"]
            ffn = layer["ffn"]

            if attn.wq is None:
                continue  # Skip if weights not loaded

            # Attention + residual
            attn_out = attn.forward(x, position=position,
                                   freq_base=self.config.get("rope_freq_base", 10000.0))
            x = x + attn_out

            # FFN + residual
            ffn_out = ffn.forward(x)
            x = x + ffn_out

        # Final RMS norm
        eps = self.config.get("attention_layer_norm_rms_epsilon", 1e-5)
        rms = np.sqrt(np.mean(x ** 2) + eps)
        x = (x / rms) * self.output_norm

        # Output head (lm_head)
        logits = self.output_head @ x  # [vocab_size]

        return logits

    def generate(self, prompt, max_tokens=256, temperature=0.8, top_k=40, top_p=0.95,
                 repeat_penalty=1.1, stream=True):
        """Generate text from prompt.

        Args:
            prompt: Input text string
            max_tokens: Maximum tokens to generate
            temperature: Sampling temperature
            top_k: Top-K filtering
            top_p: Top-P (nucleus) filtering
            repeat_penalty: Penalty for repeating tokens
            stream: If True, print tokens as they're generated

        Returns:
            Generated text string
        """
        if not self._loaded:
            raise RuntimeError("Call build() and load() first!")

        # Update sampler settings
        self.sampler = Sampler(temperature=temperature, top_k=top_k, top_p=top_p,
                              repeat_penalty=repeat_penalty)

        # Encode prompt
        input_ids = self.tokenizer.encode(prompt)
        if not input_ids:
            input_ids = [self.tokenizer.bos_id]

        context_len = self.config["context_length"]
        if len(input_ids) > context_len - max_tokens:
            input_ids = input_ids[-(context_len - max_tokens):]

        print(f"\n[Generating] Prompt: {len(input_ids)} tokens, max output: {max_tokens}")
        if stream:
            print(f"\n--- Response ---")

        generated_ids = list(input_ids)

        # Reset KV caches
        for layer in self.layers:
            layer["attn"].reset_cache(min(context_len, len(input_ids) + max_tokens))

        # Prefill: process all input tokens
        t0 = time.time()
        last_logits = None
        for pos, tid in enumerate(input_ids):
            last_logits = self._forward_token(tid, position=pos)

        prefill_time = time.time() - t0
        print(f"[Prefill done: {prefill_time:.2f}s for {len(input_ids)} tokens]")
        if stream:
            print(f"[Prefill: {prefill_time:.2f}s] ", end="", flush=True)

        # Decode: generate tokens one by one
        token_count = 0
        output_tokens = []
        history = list(input_ids)

        while token_count < max_tokens:
            # Sample next token
            next_id = self.sampler.sample(last_logits, history)

            # Check for EOS
            if next_id == self.tokenizer.eos_id:
                break

            output_tokens.append(next_id)
            history.append(next_id)

            # Decode and print if streaming
            if stream:
                text = self.tokenizer.decode([next_id])
                if text:
                    print(text, end="", flush=True)

            token_count += 1

            # Forward pass for new token
            new_pos = len(input_ids) + token_count - 1
            last_logits = self._forward_token(next_id, position=new_pos)

            # Speed info every 10 tokens
            if token_count % 10 == 0 and stream:
                elapsed = time.time() - t0
                tps = token_count / elapsed
                print(f"\n[{token_count}/{max_tokens} | {tps:.1f} tok/s] ", end="", flush=True)

        total_time = time.time() - t0
        total_tokens = len(input_ids) + token_count

        if stream:
            print()

        # Full decode
        full_output = self.tokenizer.decode(output_tokens)
        print(f"\n[Done: {token_count} tokens in {total_time:.2f}s "
              f"({token_count/total_time:.1f} tok/s)]")

        return full_output

    def close(self):
        """Clean up resources."""
        if self.gguf:
            self.gguf.close()
