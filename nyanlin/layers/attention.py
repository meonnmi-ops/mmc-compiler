"""
NYANLIN Attention Layer - Multi-head attention with GQA support

Fixed issues:
  - 'seq_အရှည်' -> 'seq_len' (Myanmar text removal)
  - '_mat_vec_optimized' -> '_mat_vec' (function name consistency)
"""

import numpy as np


def _mat_vec(W, x):
    """Multiply matrix W (shape: [out_dim, in_dim]) by vector x (shape: [in_dim]).
    Returns shape: [out_dim].

    This is equivalent to W @ x for 2D @ 1D, but handles edge cases.
    """
    if W.ndim == 1:
        return np.dot(W, x)
    return np.dot(W, x)


def _rms_norm(x, weight, eps=1e-5):
    """RMS normalization: x * weight / sqrt(mean(x^2) + eps)."""
    rms = np.sqrt(np.mean(x.astype(np.float32) ** 2) + eps)
    return (x.astype(np.float32) / rms) * weight


def _silu(x):
    """SiLU activation: x * sigmoid(x)."""
    return x * (1.0 / (1.0 + np.exp(-np.clip(x, -88, 88))))


def _apply_rope(x, n_heads, head_dim, freq_base=10000.0, position=0):
    """Apply Rotary Position Embedding (RoPE) to query/key vectors.

    x: shape [n_heads, seq_len, head_dim] or [n_heads, head_dim]
    """
    if x.ndim == 2:
        # Single position: [n_heads, head_dim]
        seq_len = 1
        x = x[:, np.newaxis, :]  # -> [n_heads, 1, head_dim]
    else:
        seq_len = x.shape[1]

    # Compute rotation frequencies
    freqs = 1.0 / (freq_base ** (np.arange(0, head_dim, 2, dtype=np.float32) / head_dim))

    # Apply position offset
    positions = np.arange(position, position + seq_len, dtype=np.float32)
    angles = np.outer(positions, freqs)  # [seq_len, head_dim/2]

    cos_vals = np.cos(angles).astype(np.float32)  # [seq_len, head_dim/2]
    sin_vals = np.sin(angles).astype(np.float32)  # [seq_len, head_dim/2]

    # Split x into pairs and rotate
    x_even = x[:, :, 0::2]  # [n_heads, seq_len, head_dim/2]
    x_odd = x[:, :, 1::2]   # [n_heads, seq_len, head_dim/2]

    # Broadcast: cos_vals, sin_vals are [seq_len, head_dim/2]
    # x_even is [n_heads, seq_len, head_dim/2]
    rotated_even = x_even * cos_vals[np.newaxis, :, :] - x_odd * sin_vals[np.newaxis, :, :]
    rotated_odd = x_even * sin_vals[np.newaxis, :, :] + x_odd * cos_vals[np.newaxis, :, :]

    # Interleave back
    result = np.zeros_like(x)
    result[:, :, 0::2] = rotated_even
    result[:, :, 1::2] = rotated_odd

    if result.shape[1] == 1 and seq_len == 1:
        return result[:, 0, :]  # Remove seq dim for single position

    return result


class Attention:
    """Multi-head attention with Grouped Query Attention (GQA) support."""

    def __init__(self, dim, n_heads, n_kv_heads, head_dim=None, eps=1e-5):
        self.dim = dim
        self.n_heads = n_heads
        self.n_kv_heads = n_kv_heads
        self.head_dim = head_dim or (dim // n_heads)
        self.n_rep = n_heads // n_kv_heads  # GQA repetition factor
        self.eps = eps

        # Weights (set externally during generator.build())
        self.wq = None  # [dim, n_heads * head_dim]
        self.wk = None  # [dim, n_kv_heads * head_dim]
        self.wv = None  # [dim, n_kv_heads * head_dim]
        self.wo = None  # [n_heads * head_dim, dim]
        self.w_norm = None  # RMSNorm weight [dim]

        # KV cache
        self.k_cache = None  # [n_kv_heads, max_seq, head_dim]
        self.v_cache = None  # [n_kv_heads, max_seq, head_dim]
        self.cache_pos = 0  # Current position in cache

    def reset_cache(self, max_seq_len):
        """Reset KV cache for new generation."""
        self.k_cache = np.zeros((self.n_kv_heads, max_seq_len, self.head_dim), dtype=np.float32)
        self.v_cache = np.zeros((self.n_kv_heads, max_seq_len, self.head_dim), dtype=np.float32)
        self.cache_pos = 0

    def forward(self, x, position=0, freq_base=10000.0):
        """Forward pass through attention layer.

        Args:
            x: Input hidden state [dim]
            position: Current token position (for RoPE)
            freq_base: RoPE frequency base

        Returns:
            output: Attention output [dim]
        """
        # RMSNorm
        h = _rms_norm(x, self.w_norm, self.eps)

        # Compute Q, K, V projections
        q = _mat_vec(self.wq, h)  # [n_heads * head_dim]
        k = _mat_vec(self.wk, h)  # [n_kv_heads * head_dim]
        v = _mat_vec(self.wv, h)  # [n_kv_heads * head_dim]

        # Reshape to [n_heads/n_kv_heads, head_dim]
        q = q.reshape(self.n_heads, self.head_dim)
        k = k.reshape(self.n_kv_heads, self.head_dim)
        v = v.reshape(self.n_kv_heads, self.head_dim)

        # Apply RoPE to Q and K
        q = _apply_rope(q, self.n_heads, self.head_dim, freq_base, position)  # [n_heads, head_dim]
        k = _apply_rope(k, self.n_kv_heads, self.head_dim, freq_base, position)  # [n_kv_heads, head_dim]

        # Store K, V in cache
        pos = self.cache_pos
        self.k_cache[:, pos, :] = k
        self.v_cache[:, pos, :] = v
        self.cache_pos = pos + 1

        seq_len = pos + 1  # How many tokens in cache

        # Compute attention scores
        scale = 1.0 / np.sqrt(self.head_dim)
        output = np.zeros(self.dim, dtype=np.float32)

        for head in range(self.n_heads):
            # Get query for this head
            q_h = q[head]  # [head_dim]

            # Map to KV head (GQA: multiple Q heads share one KV head)
            kv_head = head // self.n_rep

            # Get all cached keys and values for this KV head
            k_all = self.k_cache[kv_head, :seq_len, :]  # [seq_len, head_dim]
            v_all = self.v_cache[kv_head, :seq_len, :]  # [seq_len, head_dim]

            # Attention scores: [seq_len]
            scores = np.dot(k_all, q_h) * scale  # [seq_len]

            # Softmax
            scores = scores - np.max(scores)  # Numerical stability
            exp_scores = np.exp(scores)
            attention = exp_scores / np.sum(exp_scores)

            # Weighted sum
            attended = np.dot(attention, v_all)  # [head_dim]

            # Store in output
            start = head * self.head_dim
            end = start + self.head_dim
            output[start:end] = attended

        # Output projection
        output = _mat_vec(self.wo, output)  # [dim]

        return output
