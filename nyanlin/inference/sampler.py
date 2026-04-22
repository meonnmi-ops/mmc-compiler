"""
NYANLIN Sampler - Text generation sampling strategies

Fixed issues:
  - 'argအမြင့်ဆုံး' -> 'argmax' (Myanmar text removal)
"""

import numpy as np


class Sampler:
    """Sampling strategies for text generation."""

    def __init__(self, temperature=0.8, top_k=40, top_p=0.95, repeat_penalty=1.1):
        self.temperature = max(temperature, 0.01)  # Clamp to avoid div by 0
        self.top_k = top_k
        self.top_p = top_p
        self.repeat_penalty = repeat_penalty

    def sample(self, logits, token_ids_history=None):
        """Sample next token from logits.

        Args:
            logits: Raw model output [vocab_size]
            token_ids_history: Previous token IDs for repeat penalty

        Returns:
            token_id: Sampled token ID
        """
        logits = logits.astype(np.float64)

        # Apply repeat penalty
        if token_ids_history and self.repeat_penalty != 1.0:
            for tid in set(token_ids_history):
                if 0 <= tid < len(logits):
                    if logits[tid] > 0:
                        logits[tid] /= self.repeat_penalty
                    else:
                        logits[tid] *= self.repeat_penalty

        # Apply temperature
        logits = logits / self.temperature

        # Top-K filtering
        if self.top_k > 0 and self.top_k < len(logits):
            top_k = min(self.top_k, len(logits))
            top_k_indices = np.argpartition(logits, -top_k)[-top_k:]
            mask = np.full(len(logits), -np.inf)
            mask[top_k_indices] = logits[top_k_indices]
            logits = mask

        # Top-P (nucleus) filtering
        if 0 < self.top_p < 1.0:
            sorted_indices = np.argsort(logits)[::-1]
            sorted_logits = logits[sorted_indices]
            probabilities = np.exp(sorted_logits - np.max(sorted_logits))
            probabilities = probabilities / np.sum(probabilities)

            cumulative_probs = np.cumsum(probabilities)
            # Remove tokens with cumulative probability above top_p
            remove_mask = cumulative_probs > self.top_p
            # Keep at least one token
            remove_mask[0] = False
            sorted_logits[remove_mask] = -np.inf

            # Put back in original order
            logits[sorted_indices] = sorted_logits

        # Softmax to get probabilities
        logits = logits - np.max(logits)  # Numerical stability
        exp_logits = np.exp(logits)
        probabilities = exp_logits / np.sum(exp_logits)

        # Sample
        token_id = np.random.choice(len(probabilities), p=probabilities)
        return int(token_id)

    def greedy(self, logits):
        """Greedy sampling: always pick the most likely token."""
        return int(np.argmax(logits))
