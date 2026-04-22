"""
NYANLIN BPE Tokenizer - SentencePiece compatible tokenizer

Fixed issues:
  - O(n^2) list concatenation -> O(n) list multiplication
  - Added vocab_to_id dict for O(1) lookup instead of O(n) linear search
"""

import struct

LAZY_MARKER = "__lazy__"


class BPETokenizer:
    """BPE tokenizer that reads from GGUF metadata."""

    def __init__(self, gguf_reader):
        self.reader = gguf_reader
        self.vocab = []
        self.vocab_scores = []
        self.merges = {}
        self._vocab_to_id = {}  # For O(1) lookup
        self.bos_id = 1
        self.eos_id = 2
        self.unk_id = 0

        self._load_from_gguf()

    def _load_from_gguf(self):
        """Load tokenizer data from GGUF metadata.

        Supports lazy loading: large arrays are read directly from file
        instead of being copied from metadata dict, saving memory.
        """
        m = self.reader.metadata
        reader = self.reader

        # Helper: get value, loading from file if lazy
        def get_array(key):
            val = m.get(key, [])
            if isinstance(val, tuple) and len(val) == 2 and val[0] == LAZY_MARKER:
                print(f"[Tokenizer] Lazy loading {key} ({val[1]} items from file)...")
                val = reader.read_metadata_array(key)
                # Free the lazy placeholder from metadata
                m.pop(key, None)
            return val

        # Load tokens
        tokens = get_array("tokenizer.ggml.tokens")
        scores = get_array("tokenizer.ggml.scores")
        token_types = get_array("tokenizer.ggml.token_type")

        if not tokens:
            raise ValueError("No tokenizer tokens found in GGUF metadata")

        # Pre-allocate lists with correct size (O(n), NOT O(n^2) list concatenation!)
        n = len(tokens)
        self.vocab = tokens
        self.vocab_scores = list(scores) if scores and not (isinstance(scores, tuple) and scores[0] == LAZY_MARKER) else [0.0] * n

        # Build reverse lookup dict for O(1) token->id mapping
        self._vocab_to_id = {}
        for i, token in enumerate(self.vocab):
            self._vocab_to_id[token] = i

        # Load BPE merges
        merge_strs = get_array("tokenizer.ggml.merges")
        for i, merge_str in enumerate(merge_strs):
            parts = merge_str.split(" ", 1)
            if len(parts) == 2:
                key = (parts[0], parts[1])
                self.merges[key] = i  # rank-based merge priority

        # Special tokens
        self.bos_id = m.get("tokenizer.ggml.bos_token_id", 1)
        self.eos_id = m.get("tokenizer.ggml.eos_token_id", 2)
        self.unk_id = m.get("tokenizer.ggml.unknown_token_id", 0)

        # Add special token strings if present
        self.added_tokens = {}
        for key in m:
            if key.startswith("tokenizer.ggml.added_tokens"):
                val = m[key]
                if isinstance(val, str) and " " in val:
                    parts = val.split(" ", 1)
                    try:
                        self.added_tokens[parts[1]] = int(parts[0])
                        # Also add to vocab_to_id
                        self._vocab_to_id[parts[1]] = int(parts[0])
                    except (ValueError, IndexError):
                        pass

        # Check for prepend_bos / add_bos
        self.add_bos = m.get("tokenizer.ggml.add_bos_token", False)
        self.add_eos = m.get("tokenizer.ggml.add_eos_token", False)

        print(f"[Tokenizer] Loaded: {len(self.vocab)} tokens, {len(self.merges)} merges")
        print(f"[Tokenizer] BOS={self.bos_id}, EOS={self.eos_id},UNK={self.unk_id}")
        print(f"[Tokenizer] add_bos={self.add_bos}, add_eos={self.add_eos}")

    def _get_pairs(self, word):
        """Get set of adjacent symbol pairs in a word."""
        pairs = set()
        prev = word[0]
        for char in word[1:]:
            pairs.add((prev, char))
            prev = char
        return pairs

    def _bpe(self, token):
        """Apply BPE merges to a token string."""
        word = tuple(token)
        if len(word) <= 1:
            return word

        while True:
            pairs = self._get_pairs(word)
            if not pairs:
                break

            # Find the pair with lowest merge rank
            best_pair = None
            best_rank = float("inf")
            for pair in pairs:
                rank = self.merges.get(pair, float("inf"))
                if rank < best_rank:
                    best_rank = rank
                    best_pair = pair

            if best_pair is None or best_rank == float("inf"):
                break

            # Merge the best pair
            first, second = best_pair
            new_word = []
            i = 0
            while i < len(word):
                if i < len(word) - 1 and word[i] == first and word[i + 1] == second:
                    new_word.append(first + second)
                    i += 2
                else:
                    new_word.append(word[i])
                    i += 1
            word = tuple(new_word)

            if len(word) == 1:
                break

        return word

    def encode(self, text):
        """Encode text to token IDs."""
        tokens = []

        # Handle special tokens first
        if self.add_bos:
            tokens.append(self.bos_id)

        # Split into chunks and encode each
        # Simple regex-free approach: split on whitespace boundaries
        words = self._pre_tokenize(text)

        for word in words:
            bpe_result = self._bpe(word)
            for bpe_token in bpe_result:
                tid = self._vocab_to_id.get(bpe_token)
                if tid is not None:
                    tokens.append(tid)
                else:
                    # Try character by character
                    for ch in bpe_token:
                        tid = self._vocab_to_id.get(ch, self.unk_id)
                        tokens.append(tid)

        if self.add_eos:
            tokens.append(self.eos_id)

        return tokens

    def _pre_tokenize(self, text):
        """Simple pre-tokenization: split on word boundaries."""
        # SentencePiece-like: keep spaces as part of the next word
        # The '\u2581' (▁) character is used as space marker
        words = []
        current = ""
        for ch in text:
            if ch == " ":
                if current:
                    words.append(current)
                    current = ""
                current += "\u2581"  # space marker
            else:
                current += ch
        if current:
            words.append(current)
        return words

    def decode(self, token_ids):
        """Decode token IDs to text."""
        parts = []
        for tid in token_ids:
            if 0 <= tid < len(self.vocab):
                parts.append(self.vocab[tid])
        text = "".join(parts)
        # Replace space markers
        text = text.replace("\u2581", " ")
        # Clean up special control characters
        text = text.replace("\u0000", "")
        return text.strip()
