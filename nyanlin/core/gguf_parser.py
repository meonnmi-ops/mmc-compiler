"""
NYANLIN GGUF Parser - Reads GGUF v3 model files

Fixed/implemented:
  - Correct magic constant 0x46554747 (GGUF in little-endian)
  - Auto-parse in __init__
  - Proper handling of element counts (n_total) for arbitrary dims
  - Use memoryview/bytes for struct.unpack_from and numpy.frombuffer offsets
  - Correct BF16 -> float32 conversion
  - Safer handling / clear NotImplementedError for unsupported quant formats (Q5_K, Q8_1)
  - _type_size and _type_name helpers added
  - Lazy metadata arrays: store offsets for later on-demand load
"""

import struct
import gc
import numpy as np
import os
from functools import reduce
from operator import mul

# Threshold: arrays larger than this are loaded lazily (not stored in metadata dict)
LAZY_ARRAY_THRESHOLD = 5000


# GGUF magic number: "GGUF" in little-endian bytes
GGUF_MAGIC = 0x46554747  # 'GGUF' little-endian

# GGUF version
GGUF_VERSION = 3

# GGUF value types (as defined in GGUF spec)
GGUF_TYPE_UINT8 = 0
GGUF_TYPE_INT8 = 1
GGUF_TYPE_UINT16 = 2
GGUF_TYPE_INT16 = 3
GGUF_TYPE_UINT32 = 4
GGUF_TYPE_INT32 = 5
GGUF_TYPE_FLOAT32 = 6
GGUF_TYPE_BOOL = 7
GGUF_TYPE_STRING = 8
GGUF_TYPE_ARRAY = 9
GGUF_TYPE_UINT64 = 10
GGUF_TYPE_INT64 = 11
GGUF_TYPE_FLOAT64 = 12

# GGML tensor element types
GGML_TYPE_F32 = 0
GGML_TYPE_F16 = 1
GGML_TYPE_Q4_0 = 2
GGML_TYPE_Q4_1 = 3
GGML_TYPE_Q5_0 = 6
GGML_TYPE_Q5_1 = 7
GGML_TYPE_Q8_0 = 8
GGML_TYPE_Q8_1 = 9
GGML_TYPE_Q2_K = 10
GGML_TYPE_Q3_K = 11
GGML_TYPE_Q5_K = 12  # Q5_K_M is a variant; not implemented here
GGML_TYPE_Q6_K = 13
GGML_TYPE_Q8_K = 14
GGML_TYPE_IQ2_XXS = 16
GGML_TYPE_IQ2_XS = 17
GGML_TYPE_IQ3_XXS = 18
GGML_TYPE_IQ1_S = 19
GGML_TYPE_IQ4_NL = 20
GGML_TYPE_IQ3_S = 21
GGML_TYPE_IQ2_S = 22
GGML_TYPE_IQ4_XS = 23
GGML_TYPE_IQ1_M = 24
GGML_TYPE_BF16 = 30


class GGUFReader:
    """Reads GGUF v3 files - header, metadata, tensor info, tensor data."""

    def __init__(self, filepath):
        self.filepath = filepath
        self.file = None
        self.metadata = {}
        self.tensor_infos = []
        self.n_tensors = 0
        self.n_kv = 0
        self.version = 0

        # Lazy arrays: key -> (elem_type, count, file_offset)
        self._lazy_arrays = {}

        # Auto-parse on init
        self._open_and_parse()

    def _open_and_parse(self):
        """Open file and parse header + metadata + tensor infos."""
        self.file = open(self.filepath, "rb")
        self._parse_header()
        self._parse_metadata()
        # Free garbage after metadata parsing (helpful on low-RAM systems)
        gc.collect()
        self._parse_tensor_infos()
        # Keep file open for reading tensor data later
        print(f"[GGUF] Parsed: version={self.version}, tensors={self.n_tensors}, metadata keys={self.n_kv}")

    def _parse_header(self):
        """Parse GGUF header: magic, version, tensor count, kv count."""
        magic_bytes = self.file.read(4)
        if len(magic_bytes) != 4:
            raise EOFError("File too short when reading magic")
        magic = struct.unpack("<I", magic_bytes)[0]
        if magic != GGUF_MAGIC:
            raise ValueError(f"Invalid GGUF magic: 0x{magic:08X} (expected 0x{GGUF_MAGIC:08X})")

        self.version = struct.unpack("<I", self.file.read(4))[0]
        if self.version != GGUF_VERSION:
            print(f"[GGUF] Warning: version {self.version} (expected {GGUF_VERSION})")

        self.n_tensors = struct.unpack("<Q", self.file.read(8))[0]
        self.n_kv = struct.unpack("<Q", self.file.read(8))[0]

    def _read_value(self, dtype):
        """Read a single value of given GGUF type from file."""
        if dtype == GGUF_TYPE_UINT8:
            return struct.unpack("<B", self.file.read(1))[0]
        elif dtype == GGUF_TYPE_INT8:
            return struct.unpack("<b", self.file.read(1))[0]
        elif dtype == GGUF_TYPE_UINT16:
            return struct.unpack("<H", self.file.read(2))[0]
        elif dtype == GGUF_TYPE_INT16:
            return struct.unpack("<h", self.file.read(2))[0]
        elif dtype == GGUF_TYPE_UINT32:
            return struct.unpack("<I", self.file.read(4))[0]
        elif dtype == GGUF_TYPE_INT32:
            return struct.unpack("<i", self.file.read(4))[0]
        elif dtype == GGUF_TYPE_UINT64:
            return struct.unpack("<Q", self.file.read(8))[0]
        elif dtype == GGUF_TYPE_INT64:
            return struct.unpack("<q", self.file.read(8))[0]
        elif dtype == GGUF_TYPE_FLOAT32:
            return struct.unpack("<f", self.file.read(4))[0]
        elif dtype == GGUF_TYPE_FLOAT64:
            return struct.unpack("<d", self.file.read(8))[0]
        elif dtype == GGUF_TYPE_BOOL:
            return struct.unpack("<?", self.file.read(1))[0]
        elif dtype == GGUF_TYPE_STRING:
            length = struct.unpack("<Q", self.file.read(8))[0]
            return self.file.read(length).decode("utf-8", errors="replace")
        elif dtype == GGUF_TYPE_ARRAY:
            elem_type = struct.unpack("<I", self.file.read(4))[0]
            count = struct.unpack("<Q", self.file.read(8))[0]
            return [self._read_value(elem_type) for _ in range(count)]
        else:
            # Try to skip unknown type if possible, but GGUF types are fixed
            raise ValueError(f"Unknown GGUF value type: {dtype}")

    def _parse_metadata(self):
        """Parse all key-value metadata pairs."""
        for _ in range(self.n_kv):
            key_len_data = self.file.read(8)
            if not key_len_data:
                break
            key_len = struct.unpack("<Q", key_len_data)[0]
            key = self.file.read(key_len).decode("utf-8", errors="replace")

            val_type_data = self.file.read(4)
            if not val_type_data:
                break
            val_type = struct.unpack("<I", val_type_data)[0]

            if val_type == GGUF_TYPE_ARRAY:
                elem_type = struct.unpack("<I", self.file.read(4))[0]
                count = struct.unpack("<Q", self.file.read(8))[0]

                if count > LAZY_ARRAY_THRESHOLD:
                    # Record the offset and skip the array data without allocating
                    offset = self.file.tell()
                    self._skip_array_data(elem_type, count)
                    self.metadata[key] = ("__lazy__", count)
                    self._lazy_arrays[key] = (elem_type, count, offset)
                else:
                    value = [self._read_value(elem_type) for _ in range(count)]
                    self.metadata[key] = value
            else:
                value = self._read_value(val_type)
                self.metadata[key] = value

        # Align to 32-byte boundary after metadata
        self._align(32)

    def _skip_array_data(self, elem_type, count):
        """Skip over array data in file without allocating memory."""
        if elem_type == GGUF_TYPE_STRING:
            for _ in range(count):
                data = self.file.read(8)
                if not data:
                    break
                str_len = struct.unpack("<Q", data)[0]
                if str_len > 0:
                    self.file.seek(str_len, 1)
        else:
            elem_sizes = {
                GGUF_TYPE_UINT8: 1, GGUF_TYPE_INT8: 1, GGUF_TYPE_BOOL: 1,
                GGUF_TYPE_UINT16: 2, GGUF_TYPE_INT16: 2,
                GGUF_TYPE_UINT32: 4, GGUF_TYPE_INT32: 4, GGUF_TYPE_FLOAT32: 4,
                GGUF_TYPE_UINT64: 8, GGUF_TYPE_INT64: 8, GGUF_TYPE_FLOAT64: 8,
            }
            size = elem_sizes.get(elem_type, None)
            if size is None:
                # Fallback: read element by element (slow), but safe
                for _ in range(count):
                    self._read_value(elem_type)
            else:
                self.file.seek(count * size, 1)

    def read_metadata_array(self, key):
        """Read a previously-skipped large array from file.

        Used by the tokenizer to load tokens/merges on demand,
        avoiding double-storage in metadata + tokenizer.
        """
        if key not in self._lazy_arrays:
            val = self.metadata.get(key, [])
            if isinstance(val, tuple) and len(val) == 2 and val[0] == "__lazy__":
                return []  # shouldn't happen, but be defensive
            return val if isinstance(val, list) else []

        elem_type, count, offset = self._lazy_arrays[key]
        self.file.seek(offset)
        return [self._read_value(elem_type) for _ in range(count)]

    def _parse_tensor_infos(self):
        """Parse tensor information (name, shape, type, offset)."""
        for _ in range(self.n_tensors):
            name_len = struct.unpack("<Q", self.file.read(8))[0]
            if name_len > 10000:
                pos = self.file.tell()
                raise ValueError(
                    f"Tensor name too long ({name_len} bytes) at file position {pos}. "
                    f"File may be corrupted or metadata parsing misaligned."
                )
            name = self.file.read(name_len).decode("utf-8", errors="replace")

            n_dims = struct.unpack("<I", self.file.read(4))[0]
            dims = []
            for _ in range(n_dims):
                dims.append(struct.unpack("<Q", self.file.read(8))[0])

            tensor_type = struct.unpack("<I", self.file.read(4))[0]
            offset = struct.unpack("<Q", self.file.read(8))[0]

            self.tensor_infos.append({
                "name": name,
                "n_dims": n_dims,
                "dims": dims,      # stored as [ne[0], ne[1], ...]
                "type": tensor_type,
                "offset": offset,  # offset from start of data section
            })

        # Align to 32-byte boundary after tensor infos
        self._align(32)
        self.data_start = self.file.tell()

    def _align(self, alignment):
        """Align file position to given boundary."""
        pos = self.file.tell()
        aligned = (pos + alignment - 1) // alignment * alignment
        if aligned > pos:
            self.file.seek(aligned)

    def get_metadata(self, key, default=None):
        """Get a metadata value by key."""
        return self.metadata.get(key, default)

    def read_tensor_data(self, tensor_info, dtype=np.float32):
        """Read raw tensor data from file and return as numpy array.

        For quantized types, call read_tensor_as_f32() to get dequantized values.
        """
        offset = self.data_start + tensor_info["offset"]
        dims = tensor_info["dims"]
        type_size = self._type_size(tensor_info["type"], dims)
        self.file.seek(offset)
        raw = self.file.read(type_size)
        # If caller expects raw bytes as uint8, they can pass dtype=np.uint8
        return np.frombuffer(raw, dtype=dtype)

    def read_tensor_as_f32(self, tensor_info):
        """Read and dequantize tensor data as float32 numpy array."""
        tensor_type = tensor_info["type"]
        dims = tensor_info["dims"]  # list of dims
        # Determine shape and total number of elements
        if len(dims) == 0:
            shape = ()
            n_total = 1
        elif len(dims) == 1:
            n_total = dims[0]
            shape = (n_total,)
        else:
            n_total = reduce(mul, dims, 1)
            # GGUF/ggml uses dims stored as [ne[0], ne[1]] where ne[0]=cols inner dim, but keep shape consistent with dims order
            shape = tuple(dims)

        offset = self.data_start + tensor_info["offset"]
        self.file.seek(offset)

        if tensor_type == GGML_TYPE_F32:
            raw = self.file.read(n_total * 4)
            data = np.frombuffer(raw, dtype="<f4").reshape(shape).astype(np.float32)

        elif tensor_type == GGML_TYPE_F16:
            raw = self.file.read(n_total * 2)
            data = np.frombuffer(raw, dtype="<f2").astype(np.float32).reshape(shape)

        elif tensor_type == GGML_TYPE_BF16:
            # BF16 stored as uint16 per element. Convert to float32 by left-shifting 16 bits.
            raw = self.file.read(n_total * 2)
            u16 = np.frombuffer(raw, dtype="<u2").astype(np.uint32)
            u32 = (u16 << 16).view(np.uint32)
            data = u32.view(np.float32).reshape(shape)

        elif tensor_type == GGML_TYPE_Q8_0:
            data = self._dequantize_q8_0(*dims)  # expects (n_rows, n_cols) or (n_total,) for 1D

        elif tensor_type == GGML_TYPE_Q4_0:
            data = self._dequantize_q4_0(*dims)

        elif tensor_type == GGML_TYPE_Q5_K:
            # Complex format - not implemented here
            return self._dequantize_q5_K_M(*dims)

        elif tensor_type == GGML_TYPE_Q4_1:
            data = self._dequantize_q4_1(*dims)

        elif tensor_type == GGML_TYPE_Q8_1:
            # Not implemented here
            return self._dequantize_q8_1(*dims)

        else:
            raise ValueError(f"Unsupported tensor type: {tensor_type} ({self._type_name(tensor_type)}) for tensor '{tensor_info['name']}'")

        return data

    def _type_size(self, tensor_type, dims):
        """Return number of bytes occupied by tensor data for given type and dims."""
        if len(dims) == 0:
            n_total = 1
        elif len(dims) == 1:
            n_total = dims[0]
        else:
            n_total = reduce(mul, dims, 1)

        if tensor_type == GGML_TYPE_F32:
            return n_total * 4
        elif tensor_type == GGML_TYPE_F16 or tensor_type == GGML_TYPE_BF16:
            return n_total * 2
        elif tensor_type == GGML_TYPE_Q8_0:
            QK = 32
            block_size = 2 + QK
            n_blocks = (n_total + QK - 1) // QK
            return n_blocks * block_size
        elif tensor_type == GGML_TYPE_Q4_0:
            QK = 32
            block_size = 2 + QK // 2
            n_blocks = (n_total + QK - 1) // QK
            return n_blocks * block_size
        elif tensor_type == GGML_TYPE_Q4_1:
            QK = 32
            block_size = 2 + 2 + QK // 2
            n_blocks = (n_total + QK - 1) // QK
            return n_blocks * block_size
        elif tensor_type in (GGML_TYPE_Q5_K, GGML_TYPE_Q8_1):
            # Complex / variable formats - unknown here, raise to prevent silent misreads
            raise NotImplementedError(f"_type_size not implemented for tensor type {tensor_type} ({self._type_name(tensor_type)})")
        else:
            raise NotImplementedError(f"_type_size not implemented for tensor type {tensor_type} ({self._type_name(tensor_type)})")

    def _type_name(self, t):
        names = {
            GGML_TYPE_F32: "F32", GGML_TYPE_F16: "F16", GGML_TYPE_Q4_0: "Q4_0", GGML_TYPE_Q4_1: "Q4_1",
            GGML_TYPE_Q5_0: "Q5_0", GGML_TYPE_Q5_1: "Q5_1", GGML_TYPE_Q8_0: "Q8_0", GGML_TYPE_Q8_1: "Q8_1",
            GGML_TYPE_Q5_K: "Q5_K", GGML_TYPE_BF16: "BF16"
        }
        return names.get(t, f"TYPE_{t}")

    # ---------------------------
    # Dequantizers
    # ---------------------------

    def _dequantize_q8_0(self, *dims):
        """Dequantize Q8_0 blocks.

        Signature accepts either (n_elements,) or (n_cols, n_rows) / (n_rows, n_cols).
        Internally computes n_total = product(dims).
        Each block: 2 bytes float16 scale (little-endian) + 32 int8 values.
        """
        QK = 32
        block_size = 2 + QK

        # compute n_total and shape
        if len(dims) == 0:
            n_total = 1
            shape = ()
        elif len(dims) == 1:
            n_total = dims[0]
            shape = (n_total,)
        else:
            n_total = reduce(mul, dims, 1)
            shape = tuple(dims)

        n_blocks = (n_total + QK - 1) // QK
        raw = self.file.read(n_blocks * block_size)
        if len(raw) != n_blocks * block_size:
            raise EOFError("Unexpected EOF in Q8_0 data")

        mv = memoryview(raw)
        result = np.empty(n_blocks * QK, dtype=np.float32)[:n_total]

        for i in range(n_blocks):
            off = i * block_size
            # struct.unpack_from works with bytes-like objects (memoryview)
            scale = struct.unpack_from("<e", mv, off)[0]  # 'e' = float16
            # read QK int8 values from raw at offset off+2
            count = min(QK, n_total - i * QK)
            vals = np.frombuffer(raw, dtype=np.int8, count=count, offset=off + 2)
            start = i * QK
            result[start:start + count] = vals.astype(np.float32) * float(scale)

        return result.reshape(shape)

    def _dequantize_q4_0(self, *dims):
        """Dequantize Q4_0 blocks: 2-byte float16 scale + 16 bytes packed (32 nibbles).
        Nibbles represent signed values in range -8..7 (packed: low nibble, high nibble).
        """
        QK = 32
        block_size = 2 + QK // 2
        if len(dims) == 0:
            n_total = 1
            shape = ()
        elif len(dims) == 1:
            n_total = dims[0]
            shape = (n_total,)
        else:
            n_total = reduce(mul, dims, 1)
            shape = tuple(dims)

        n_blocks = (n_total + QK - 1) // QK
        raw = self.file.read(n_blocks * block_size)
        if len(raw) != n_blocks * block_size:
            raise EOFError("Unexpected EOF in Q4_0 data")

        mv = memoryview(raw)
        result = np.empty(n_blocks * QK, dtype=np.float32)[:n_total]

        for i in range(n_blocks):
            off = i * block_size
            scale = struct.unpack_from("<e", mv, off)[0]
            packed = np.frombuffer(raw, dtype=np.uint8, count=QK // 2, offset=off + 2)
            start = i * QK
            for j in range(len(packed)):
                byte = int(packed[j])
                low = (byte & 0x0F) - 8
                high = (byte >> 4) - 8
                idx = start + j * 2
                if idx < n_total:
                    result[idx] = float(low) * float(scale)
                if idx + 1 < n_total:
                    result[idx + 1] = float(high) * float(scale)

        return result.reshape(shape)

    def _dequantize_q4_1(self, *dims):
        """Dequantize Q4_1 blocks: float16 scale + float16 minimum + 16 bytes packed nibbles (32 values).
        Each nibble is unsigned 0..15; final = minimum + nibble * scale
        """
        QK = 32
        block_size = 2 + 2 + QK // 2
        if len(dims) == 0:
            n_total = 1
            shape = ()
        elif len(dims) == 1:
            n_total = dims[0]
            shape = (n_total,)
        else:
            n_total = reduce(mul, dims, 1)
            shape = tuple(dims)

        n_blocks = (n_total + QK - 1) // QK
        raw = self.file.read(n_blocks * block_size)
        if len(raw) != n_blocks * block_size:
            raise EOFError("Unexpected EOF in Q4_1 data")

        mv = memoryview(raw)
        result = np.empty(n_blocks * QK, dtype=np.float32)[:n_total]

        for i in range(n_blocks):
            off = i * block_size
            scale = struct.unpack_from("<e", mv, off)[0]
            minimum = struct.unpack_from("<e", mv, off + 2)[0]
            packed = np.frombuffer(raw, dtype=np.uint8, count=QK // 2, offset=off + 4)
            start = i * QK
            for j in range(len(packed)):
                byte = int(packed[j])
                low = float(byte & 0x0F)
                high = float(byte >> 4)
                idx = start + j * 2
                if idx < n_total:
                    result[idx] = float(minimum) + low * float(scale)
                if idx + 1 < n_total:
                    result[idx + 1] = float(minimum) + high * float(scale)

        return result.reshape(shape)

    def _dequantize_q5_K_M(self, *dims):
        """Placeholder for Q5_K/Q5_K_M dequantization.

        Q5_K family has a complex layout (super-blocks + bit packing). Implementing
        a correct and performant dequantizer requires the exact spec and careful bit ops.

        For now we raise NotImplementedError so callers know it's unsupported.
        """
        raise NotImplementedError("Dequantization for Q5_K (Q5_K_M) is not implemented in this parser.")

    def _dequantize_q8_1(self, *dims):
        """Placeholder for Q8_1 dequantization.

        Q8_1 has a different layout than Q8_0. Implement when spec & tests available.
        """
        raise NotImplementedError("Dequantization for Q8_1 is not implemented in this parser.")


# End of gguf_parser.py