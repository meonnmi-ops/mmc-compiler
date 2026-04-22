"""
NYANLIN Model Loader - Loads model config from GGUF metadata

Fixed issues:
  - Added arch tracking (qwen2, llama, mistral)
  - Added intermediate_size fallback for Qwen2
  - "Mo" -> "Model" text fix
"""

from .gguf_parser import GGUFReader


class ModelLoader:
    """Loads model configuration from GGUF metadata."""

    def __init__(self, gguf_reader):
        self.reader = gguf_reader
        self.metadata = gguf_reader.metadata
        self.arch = None
        self.config = {}

        self._detect_architecture()
        self._load_config()

    def _detect_architecture(self):
        """Detect model architecture from metadata."""
        self.arch = self.metadata.get("general.architecture")
        if self.arch:
            print(f"[ModelLoader] Architecture: {self.arch}")
        else:
            print("[ModelLoader] Warning: No architecture found in metadata")

    def _load_config(self):
        """Load all model configuration from metadata."""
        m = self.metadata
        arch = self.arch

        if arch == "qwen2":
            self.config = {
                "arch": "qwen2",
                "block_count": m.get("qwen2.block_count", 0),
                "embedding_length": m.get("qwen2.embedding_length", 0),
                "context_length": m.get("qwen2.context_length", 2048),
                "head_count": m.get("qwen2.attention.head_count", 0),
                "head_count_kv": m.get("qwen2.attention.head_count_kv", 0),
                "vocab_size": m.get("qwen2.vocab_size", 0),
                "rope_dim_base": m.get("qwen2.rope.dimension_base", None),
                "rope_freq_base": m.get("qwen2.rope.freq_base", 10000.0),
                "feed_forward_length": m.get("qwen2.feed_forward_length", 0),
                "attention_layer_norm_rms_epsilon": m.get("qwen2.attention.layer_norm_rms_epsilon", 1e-5),
                "expert_count": m.get("qwen2.expert_count", 0),
                "expert_used_count": m.get("qwen2.expert_used_count", 0),
            }
            # Fallback: compute intermediate_size from embedding_length if not set
            if self.config["feed_forward_length"] == 0:
                emb_len = self.config["embedding_length"]
                if emb_len > 0:
                    self.config["feed_forward_length"] = emb_len * 4
                    print(f"[ModelLoader] feed_forward_length not found, using {emb_len * 4} (4x embedding)")

        elif arch == "llama":
            self.config = {
                "arch": "llama",
                "block_count": m.get("llama.block_count", 0),
                "embedding_length": m.get("llama.embedding_length", 0),
                "context_length": m.get("llama.context_length", 2048),
                "head_count": m.get("llama.attention.head_count", 0),
                "head_count_kv": m.get("llama.attention.head_count_kv", 0),
                "vocab_size": m.get("llama.vocab_size", 0),
                "rope_dim_base": m.get("llama.rope.dimension_base", None),
                "rope_freq_base": m.get("llama.rope.freq_base", 10000.0),
                "feed_forward_length": m.get("llama.feed_forward_length", 0),
                "attention_layer_norm_rms_epsilon": m.get("llama.attention.layer_norm_rms_epsilon", 1e-5),
                "expert_count": m.get("llama.expert_count", 0),
                "expert_used_count": m.get("llama.expert_used_count", 0),
            }
            if self.config["feed_forward_length"] == 0:
                emb_len = self.config["embedding_length"]
                if emb_len > 0:
                    self.config["feed_forward_length"] = int(emb_len * 8 / 3)
                    self.config["feed_forward_length"] = ((self.config["feed_forward_length"] + 255) // 256) * 256
                    print(f"[ModelLoader] feed_forward_length not found, using {self.config['feed_forward_length']}")

        elif arch == "mistral":
            self.config = {
                "arch": "mistral",
                "block_count": m.get("mistral.block_count", 0),
                "embedding_length": m.get("mistral.embedding_length", 0),
                "context_length": m.get("mistral.context_length", 2048),
                "head_count": m.get("mistral.attention.head_count", 0),
                "head_count_kv": m.get("mistral.attention.head_count_kv", 0),
                "vocab_size": m.get("mistral.vocab_size", 0),
                "rope_freq_base": m.get("mistral.rope.freq_base", 10000.0),
                "feed_forward_length": m.get("mistral.feed_forward_length", 0),
                "attention_layer_norm_rms_epsilon": m.get("mistral.attention.layer_norm_rms_epsilon", 1e-5),
            }

        elif arch == "gemma2" or arch == "gemma":
            self.config = {
                "arch": arch,
                "block_count": m.get(f"{arch}.block_count", 0),
                "embedding_length": m.get(f"{arch}.embedding_length", 0),
                "context_length": m.get(f"{arch}.context_length", 2048),
                "head_count": m.get(f"{arch}.attention.head_count", 0),
                "head_count_kv": m.get(f"{arch}.attention.head_count_kv", 0),
                "vocab_size": m.get(f"{arch}.vocab_size", 0),
                "feed_forward_length": m.get(f"{arch}.feed_forward_length", 0),
                "attention_layer_norm_rms_epsilon": m.get(f"{arch}.attention.layer_norm_rms_epsilon", 1e-5),
            }
            if self.config["feed_forward_length"] == 0:
                emb_len = self.config["embedding_length"]
                if emb_len > 0:
                    self.config["feed_forward_length"] = emb_len * 4

        else:
            # Generic fallback
            print(f"[ModelLoader] Unknown architecture '{arch}', using generic config")
            self.config = {
                "arch": arch or "unknown",
                "block_count": 0,
                "embedding_length": m.get("general.embedding_length", 0),
                "context_length": 2048,
                "head_count": 0,
                "head_count_kv": 0,
                "vocab_size": 0,
                "feed_forward_length": 0,
            }

        print(f"[ModelLoader] Config loaded: "
              f"layers={self.config['block_count']}, "
              f"dim={self.config['embedding_length']}, "
              f"heads={self.config['head_count']}, "
              f"kv_heads={self.config['head_count_kv']}, "
              f"ff_dim={self.config['feed_forward_length']}, "
              f"vocab={self.config['vocab_size']}")

    def get(self, key, default=None):
        """Get a config value."""
        return self.config.get(key, default)
