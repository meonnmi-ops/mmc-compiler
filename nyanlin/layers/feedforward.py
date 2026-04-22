"""
NYANLIN Feed-Forward Layer - SwiGLU FFN

Fixed issues:
  - '_mat_vec_optimized' -> '_mat_vec' (function name consistency)
  - 'softအမြင့်ဆုံး' -> 'softmax' (Myanmar text removal)
  - Uses SwiGLU: down(x) -> SiLU(gate(x)) * down(x) -> up()
"""

import numpy as np


def _mat_vec(W, x):
    """Matrix-vector multiply: W @ x."""
    if W.ndim == 1:
        return np.dot(W, x)
    return np.dot(W, x)


def _silu(x):
    """SiLU (Swish) activation: x * sigmoid(x)."""
    return x * (1.0 / (1.0 + np.exp(-np.clip(x, -88, 88))))


def _rms_norm(x, weight, eps=1e-5):
    """RMS normalization."""
    rms = np.sqrt(np.mean(x.astype(np.float32) ** 2) + eps)
    return (x.astype(np.float32) / rms) * weight


class FeedForward:
    """SwiGLU Feed-Forward Network: norm -> gate_proj -> SiLU -> up_proj -> down_proj."""

    def __init__(self, dim, intermediate_dim, eps=1e-5):
        self.dim = dim
        self.intermediate_dim = intermediate_dim
        self.eps = eps

        # Weights (set externally during generator.build())
        self.w_gate = None   # [dim, intermediate_dim]
        self.w_up = None     # [dim, intermediate_dim]
        self.w_down = None   # [intermediate_dim, dim]
        self.w_norm = None   # RMSNorm weight [dim]

    def forward(self, x):
        """Forward pass: RMSNorm -> SwiGLU -> output projection.

        Args:
            x: Input hidden state [dim]

        Returns:
            output: FFN output [dim]
        """
        # RMSNorm (post-attention residual connection norm)
        h = _rms_norm(x, self.w_norm, self.eps)

        # Gate projection
        gate = _mat_vec(self.w_gate, h)  # [intermediate_dim]

        # Up projection
        up = _mat_vec(self.w_up, h)  # [intermediate_dim]

        # SwiGLU: SiLU(gate) * up
        h = _silu(gate) * up

        # Down projection
        output = _mat_vec(self.w_down, h)  # [dim]

        return output
