"""
NYANLIN Tensor - Lightweight tensor operations

Fixed issues:
  - 'to_စာရင်း' renamed to 'to_list' (Myanmar text removal)
"""


class Tensor:
    """Simple tensor wrapper around numpy arrays."""

    def __init__(self, data, shape=None):
        import numpy as np
        if isinstance(data, np.ndarray):
            self.data = data
        elif isinstance(data, list):
            self.data = np.array(data, dtype=np.float32)
        else:
            self.data = np.array([data], dtype=np.float32)

        if shape:
            self.data = self.data.reshape(shape)

    @property
    def shape(self):
        return self.data.shape

    @property
    def dtype(self):
        return self.data.dtype

    @property
    def size(self):
        return self.data.size

    def to_list(self):
        """Convert to Python list."""
        return self.data.tolist()

    def to_numpy(self):
        """Get underlying numpy array."""
        return self.data

    def __repr__(self):
        return f"Tensor(shape={self.shape}, dtype={self.dtype})"

    def __add__(self, other):
        if isinstance(other, Tensor):
            return Tensor(self.data + other.data)
        return Tensor(self.data + other)

    def __mul__(self, other):
        if isinstance(other, Tensor):
            return Tensor(self.data * other.data)
        return Tensor(self.data * other)

    def __matmul__(self, other):
        if isinstance(other, Tensor):
            return Tensor(self.data @ other.data)
        return Tensor(self.data @ other)

    def __neg__(self):
        return Tensor(-self.data)
