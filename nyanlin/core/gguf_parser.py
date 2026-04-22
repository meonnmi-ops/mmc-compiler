"""
NYANLIN GGUF Parser - Reads GGUF v3 model files

Fixed issues:
  - Correct magic constant 0x46554747 (GGUF in little-endian)
  - Removed undefined GGUF_TENSOR_F64
  - Auto-parse in __init__
  - Proper dequantization for F16, Q8_0, Q4_0, Q5_K_M
"""

import struct
import gc
import numpy as np
import os

# Threshold: arrays larger than this are loaded lazily (not stored in metadata dict)
LAZY_ARRAY_THRESHOLD = 5000


# GGUF magic number: "GGUF" in little-endian bytes
GGUF_MAGIC = 0x46554747  # NOT 0x46475547 - byte order matters!

# GGUF version
GGUF_VERSION = 3

# Tensor types (as defined in GGUF spec)
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

# GGML tensor types
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
GGML_TYPE_Q5_K = 12  # Q5_K_M is a variant
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

        # Auto-parse on init
        self._open_and_parse()

    def _open_and_parse(self):
        """Open file and parse header + metadata + tensor infos."""
        self._lazy_arrays = {}  # key -> (elem_type, count, file_offset) for large arrays
        self.file = open(self.filepath, "rb")
        self._parse_header()
        self._parse_metadata()
        # Free garbage after metadata parsing (critical for low-RAM systems)
        gc.collect()
        self._parse_tensor_infos()
        # Keep file open for reading tensor data later
        print(f"[GGUF] Parsed: version={self.version}, tensors={self.n_tensors}, metadata keys={self.n_kv}")

    def _parse_header(self):
        """Parse GGUF header: magic, version, tensor count, kv count."""
        magic = struct.unpack("<I", self.file.read(4))[0]
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
            raise ValueError(f"Unknown GGUF value type: {dtype}")

    def _parse_metadata(self):
        """Parse all key-value metadata pairs.

        Large arrays (>5000 elements) are NOT loaded into memory.
        Instead, only the file offset is recorded. Use read_metadata_array()
        to read them on demand. This is critical for low-RAM systems.
        """
        for _ in range(self.n_kv):
            # Read key (string)
            key_len = struct.unpack("<Q", self.file.read(8))[0]
            key = self.file.read(key_len).decode("utf-8", errors="replace")

            # Read value type
            val_type = struct.unpack("<I", self.file.read(4))[0]

            if val_type == GGUF_TYPE_ARRAY:
                # Read array header: elem_type + count
                elem_type = struct.unpack("<I", self.file.read(4))[0]
                count = struct.unpack("<Q", self.file.read(8))[0]

                if count > LAZY_ARRAY_THRESHOLD:
                    # LAZY: record file offset, skip over data (don't allocate!)
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
        """Skip over array data in file without allocating memory.

        Used for large arrays (tokens, merges, etc.) that are loaded lazily.
        KEY: uses seek() instead of read() to avoid any memory allocation.
        """
        if elem_type == GGUF_TYPE_STRING:
            # Read ALL string length headers in ONE call (8 bytes each)
            # Then compute total body bytes and skip with ONE seek
            header_size = count * 8
            length_data = self.file.read(header_size)
            if len(length_data) != header_size:
                raise ValueError(f"Unexpected EOF reading string array headers "
                               f"(got {len(length_data)}, expected {header_size})")
            total_body = 0
            for i in range(count):
                str_len = struct.unpack_from("<Q", length_data, i * 8)[0]
                total_body += str_len
            # ONE seek to skip all string bodies - zero memory allocation
            self.file.seek(self.file.tell() + total_body)
        else:
            elem_sizes = {
                GGUF_TYPE_UINT8: 1, GGUF_TYPE_INT8: 1, GGUF_TYPE_BOOL: 1,
                GGUF_TYPE_UINT16: 2, GGUF_TYPE_INT16: 2,
                GGUF_TYPE_UINT32: 4, GGUF_TYPE_INT32: 4, GGUF_TYPE_FLOAT32: 4,
                GGUF_TYPE_UINT64: 8, GGUF_TYPE_INT64: 8, GGUF_TYPE_FLOAT64: 8,
            }
            size = elem_sizes.get(elem_type, 0)
            if size > 0:
                self.file.seek(self.file.tell() + count * size)
            else:
                # Unknown type - must read through elements (slow but rare)
                for _ in range(count):
                    self._read_value(elem_type)

    def read_metadata_array(self, key):
        """Read a previously-skipped large array from file.

        Used by the tokenizer to load tokens/merges on demand,
        avoiding double-storage in metadata + tokenizer.
        """
        if key not in self._lazy_arrays:
            val = self.metadata.get(key, [])
            if isinstance(val, tuple) and len(val) == 2 and val[0] == "__lazy__":
                return []  # Shouldn't happen
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
                "dims": dims,      # [rows, cols] for 2D - stored as [ne[0], ne[1]]
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

        For quantized types, returns raw bytes as uint8 array.
        Use read_tensor_as_f32() for dequantized float32 arrays.
        """
        offset = self.data_start + tensor_info["offset"]
        # Calculate total bytes from dims and type
        dims = tensor_info["dims"]
        type_size = self._type_size(tensor_info["type"], dims)
        self.file.seek(offset)
        raw = self.file.read(type_size)
        return np.frombuffer(raw, dtype=dtype)

    def read_tensor_as_f32(self, tensor_info):
        """Read and dequantize tensor data as float32 numpy array."""
        tensor_type = tensor_info["type"]
        dims = tensor_info["dims"]  # [ne0, ne1] where ne0 = cols, ne1 = rows for GGUF

        offset = self.data_start + tensor_info["offset"]
        self.file.seek(offset)

        if len(dims) == 1:
            n_elements = dims[0]
            shape = (n_elements,)
        elif len(dims) == 2:
            # GGUF stores dims as [ne[0], ne[1]] where ne[0]=cols (inner dim), ne[1]=rows
            n_rows = dims[1]
            n_cols = dims[0]
            shape = (n_rows, n_cols)
        else:
            n_elements = 1
            for d in dims:
                n_elements *= d
            shape = tuple(dims)

        if tensor_type == GGML_TYPE_F32:
            raw = self.file.read(n_rows * n_cols * 4)
            data = np.frombuffer(raw, dtype=np.float32).reshape(shape)

        elif tensor_type == GGML_TYPE_F16:
            raw = self.file.read(n_rows * n_cols * 2)
            data = np.frombuffer(raw, dtype=np.float16).astype(np.float32).reshape(shape)

        elif tensor_type == GGML_TYPE_BF16:
            # BF16: bfloat16 - same as uint16 with implicit leading 1
            raw = self.file.read(n_rows * n_cols * 2)
            u16 = np.frombuffer(raw, dtype=np.uint16).reshape(shape)
            data = np.zeros(shape, dtype=np.float32)
            sign = np.where(u16 >> 15, -1.0, 1.0)
            exp = ((u16 >> 7) & 0xFF).astype(np.float32)
            mant = (u16 & 0x7F).astype(np.float32)
            data = sign * np.power(2.0, exp - 127.0) * (mant / 128.0 + 1.0)

        elif tensor_type == GGML_TYPE_Q8_0:
            data = self._dequantize_q8_0(n_rows, n_cols)

        elif tensor_type == GGML_TYPE_Q4_0:
            data = self._dequantize_q4_0(n_rows, n_cols)

        elif tensor_type == GGML_TYPE_Q5_K:
            data = self._dequantize_q5_K_M(n_rows, n_cols)

        elif tensor_type == GGML_TYPE_Q4_1:
            data = self._dequantize_q4_1(n_rows, n_cols)

        elif tensor_type == GGML_TYPE_Q8_1:
            data = self._dequantize_q8_1(n_rows, n_cols)

        else:
            raise ValueError(f"Unsupported tensor type: {tensor_type} ({self._type_name(tensor_type)}) for tensor '{tensor_info['name']}'")

        return data

    def _dequantize_q8_0(self, n_rows, n_cols):
        """Dequantize Q8_0 blocks: each block = 1 float16 scale + QK8_0 int8 values."""
        QK = 32  # block size
        block_size = 2 + QK  # scale (f16) + 32 int8 values = 34 bytes
        n_blocks = (n_rows * n_cols + QK - 1) // QK
        raw = self.file.read(n_blocks * block_size)
        raw = np.frombuffer(raw, dtype=np.uint8)

        n_total = n_rows * n_cols
        result = np.zeros(n_total, dtype=np.float32)

        for i in range(n_blocks):
            offset = i * block_size
            scale = struct.unpack_from("<e", raw, offset)[0]
            vals = raw[offset + 2: offset + 2 + QK].astype(np.int8)
            start = i * QK
            end = min(start + QK, n_total)
            result[start:end] = vals[:end - start].astype(np.float32) * scale

        return result.reshape(n_rows, n_cols)

    def _dequantize_q4_0(self, n_rows, n_cols):
        """Dequantize Q4_0 blocks: 1 f16 scale + 16 int4 values packed as 16 uint8."""
        QK = 32
        block_size = 2 + QK // 2  # scale (f16) + 16 bytes (32 nibbles) = 18 bytes
        n_blocks = (n_rows * n_cols + QK - 1) // QK
        raw = self.file.read(n_blocks * block_size)
        raw = np.frombuffer(raw, dtype=np.uint8)

        n_total = n_rows * n_cols
        result = np.zeros(n_total, dtype=np.float32)

        for i in range(n_blocks):
            offset = i * block_size
            scale = struct.unpack_from("<e", raw, offset)[0]
            packed = raw[offset + 2: offset + 2 + QK // 2]

            start = i * QK
            for j in range(QK // 2):
                byte = packed[j]
                low = (byte & 0x0F) - 8
                high = (byte >> 4) - 8
                idx = start + j * 2
                if idx < n_total:
                    result[idx] = float(low) * scale
                if idx + 1 < n_total:
                    result[idx + 1] = float(high) * scale

        return result.reshape(n_rows, n_cols)

    def _dequantize_q4_1(self, n_rows, n_cols):
        """Dequantize Q4_1 blocks: f16 scale + f16 min + 16 nibbles."""
        QK = 32
        block_size = 2 + 2 + QK // 2  # scale(f16) + min(f16) + 16 bytes = 20 bytes
        n_blocks = (n_rows * n_cols + QK - 1) // QK
        raw = self.file.read(n_blocks * block_size)
        raw = np.frombuffer(raw, dtype=np.uint8)

        n_total = n_rows * n_cols
        result = np.zeros(n_total, dtype=np.float32)

        for i in range(n_blocks):
            offset = i * block_size
            scale = struct.unpack_from("<e", raw, offset)[0]
            minimum = struct.unpack_from("<e", raw, offset + 2)[0]
            packed = raw[offset + 4: offset + 4 + QK // 2]

            start = i * QK
            for j in range(QK // 2):
                byte = packed[j]
                low = float(byte & 0x0F)
                high = float(byte >> 4)
                idx = start + j * 2
                if idx < n_total:
                    result[idx] = minimum + low * scale
                if idx + 1 < n_total:
                    result[idx + 1] = minimum + high * scale

        return result.reshape(n_rows, n_cols)

    def _dequantize_q5_K_M(self, n_rows, n_cols):
        """Dequantize Q5_K blocks (type 12 = Q5_K).

        Q5_K_M block layout (256 values per super-block):
          - 2 bytes: uint16 d (super scale)
          - 2 bytes: uint16 dmin (super minimum)
          - 12 bytes: 8 × uint16 qs (scales, packed 4-bit each, 8 groups)
          - 4 bytes: uint32 qh (high bits for q5)
          - 128 bytes: 128 × uint8 ql (low 4 bits, packed as ql[i] = low_nibble(i) | high_nibble(i+1)<<4)

        Actually Q5_K layout (per 256-element super-block):
          - 2 bytes: d (uint16 scale)
          - 2 bytes: dmin (uint16 min)
          - 12 bytes: scales (uint8[12] - packed 6-bit scales for 8 sub-blocks)
          - 4 bytes: qh (uint32 - high bits)
          - 128 bytes: ql (uint8[128] - 4-bit low parts, two per byte)
        """
        K = 256  # super-block size
        # Super-block: 2 + 2 + 12 + 4 + 128 = 148 bytes
        super_block_size = 148
        n_super_blocks = (n_rows * n_cols + K - 1) // K
        raw = self.file.read(n_super_blocks * super_block_size)
        raw = np.frombuffer(raw, dtype=np.uint8)

        n_total = n_rows * n_cols
        result = np.zeros(n_total, dtype=np.float32)

        for sb in range(n_super_blocks):
            sb_offset = sb * super_block_size
            d = struct.unpack_from("<H", raw, sb_offset)[0]
            d_min = struct.unpack_from("<H", raw, sb_offset + 2)[0]

            # Scales: 12 bytes for 8 sub-blocks (6-bit each)
            scales_data = raw[sb_offset + 4: sb_offset + 16]

            # Unpack 6-bit scales from 12 bytes
            scales = []
            for j in range(4):
                byte0 = scales_data[j * 3 + 0]
                byte1 = scales_data[j * 3 + 1]
                byte2 = scales_data[j * 3 + 2]
                scales.append(byte0 & 0x3F)
                scales.append((byte1 & 0x0F) | ((byte0 >> 6) << 4))
                scales.append((byte2 & 0x03) | ((byte1 >> 4) << 2))
                scales.append((byte2 >> 2) & 0x3F)

            # Convert to signed scales
            scales = [s - 32 if s > 31 else s for s in scales]

            # Qh: high bits (4 bytes for 256 values)
            qh = struct.unpack_from("<I", raw, sb_offset + 16)[0]

            # QL: low bits (128 bytes, two 4-bit values per byte)
            ql = raw[sb_offset + 20: sb_offset + 148]

            d_float = float(d)
            d_min_float = float(d_min)

            sb_start = sb * K
            for i in range(128):
                byte = ql[i]
                # Two 4-bit values packed in each byte of ql
                low0 = byte & 0x0F
                low1 = (byte >> 4) & 0x0F

                # High bits from qh (1 bit per value)
                h0 = (qh >> (i * 2)) & 1
                h1 = (qh >> (i * 2 + 1)) & 1

                # Combine: value = high_bit << 4 | low_4bits
                val0 = (h0 << 4) | low0
                val1 = (h1 << 4) | low1

                idx0 = sb_start + i * 2
                idx1 = sb_start + i * 2 + 1

                if idx0 < n_total:
                    sub_block = i // 32  # 128 values / 4 sub-blocks = 32 each... wait
                    # Actually 256 values / 8 sub-blocks = 32 values per sub-block
                    # But we iterate 128 ql bytes × 2 = 256 values
                    sub_idx = idx0 % 256
                    sub_block_idx = sub_idx // 32
                    s = scales[sub_block_idx] if sub_block_idx < len(scales) else 1
                    result[idx0] = (d_float * s + d_min_float) * float(val0 - 16)

                if idx1 < n_total:
                    sub_idx = idx1 % 256
                    sub_block_idx = sub_idx // 32
                    s = scales[sub_block_idx] if sub_block_idx < len(scales) else 1
                    result[idx1] = (d_float * s + d_min_float) * float(val1 - 16)

        return result.reshape(n_rows, n_cols)

    def _dequantize_q8_1(self, n_rows, n_cols):
        """Dequantize Q8_1 blocks: f16 scale + f16 min + 32 int8 values."""
        QK = 32
        block_size = 2 + 2 + QK  # scale + min + 32 values = 36 bytes
        n_blocks = (n_rows * n_cols + QK - 1) // QK
        raw = self.file.read(n_blocks * block_size)
        raw = np.frombuffer(raw, dtype=np.uint8)

        n_total = n_rows * n_cols
        result = np.zeros(n_total, dtype=np.float32)

        for i in range(n_blocks):
            offset = i * block_size
            scale = struct.unpack_from("<e", raw, offset)[0]
            minimum = struct.unpack_from("<e", raw, offset + 2)[0]
            vals = raw[offset + 4: offset + 4 + QK].astype(np.int8)
            start = i * QK
            end = min(start + QK, n_total)
            result[start:end] = minimum + vals[:end - start].astype(np.float32) * scale

        return result.reshape(n_rows, n_cols)

    def _type_size(self, tensor_type, dims):
        """Calculate byte size of tensor data."""
        if len(dims) >= 2:
            n = dims[0] * dims[1]
        elif len(dims) == 1:
            n = dims[0]
        else:
            n = 1

        type_sizes = {
            GGML_TYPE_F32: 4,
            GGML_TYPE_F16: 2,
            GGML_TYPE_BF16: 2,
        }
        if tensor_type in type_sizes:
            return n * type_sizes[tensor_type]

        # Block-quantized types
        block_sizes = {
            GGML_TYPE_Q8_0: 34,    # 2 + 32
            GGML_TYPE_Q8_1: 36,    # 2 + 2 + 32
            GGML_TYPE_Q4_0: 18,    # 2 + 16
            GGML_TYPE_Q4_1: 20,    # 2 + 2 + 16
            GGML_TYPE_Q5_K: 148,   # 2 + 2 + 12 + 4 + 128
        }
        block_qs = {
            GGML_TYPE_Q8_0: 32,
            GGML_TYPE_Q8_1: 32,
            GGML_TYPE_Q4_0: 32,
            GGML_TYPE_Q4_1: 32,
            GGML_TYPE_Q5_K: 256,
        }

        if tensor_type in block_sizes:
            bs = block_sizes[tensor_type]
            bq = block_qs[tensor_type]
            n_blocks = (n + bq - 1) // bq
            return n_blocks * bs

        raise ValueError(f"Unknown tensor type: {tensor_type}")

    @staticmethod
    def _type_name(tensor_type):
        """Return human-readable type name."""
        names = {
            0: "F32", 1: "F16", 2: "Q4_0", 3: "Q4_1",
            6: "Q5_0", 7: "Q5_1", 8: "Q8_0", 9: "Q8_1",
            10: "Q2_K", 11: "Q3_K", 12: "Q5_K", 13: "Q6_K",
            14: "Q8_K", 30: "BF16",
        }
        return names.get(tensor_type, f"UNKNOWN({tensor_type})")

    def print_summary(self):
        """Print model summary."""
        print(f"\n=== GGUF Model Summary ===")
        print(f"File: {os.path.basename(self.filepath)}")
        print(f"Version: {self.version}")
        print(f"Tensors: {self.n_tensors}")
        print(f"Metadata keys: {self.n_kv}")

        # Key metadata
        for key in ["general.architecture", "general.name", "general.file_type",
                     "qwen2.block_count", "llama.block_count", "mistral.block_count",
                     "qwen2.embedding_length", "llama.embedding_length",
                     "qwen2.attention.head_count", "llama.attention.head_count",
                     "qwen2.attention.head_count_kv", "llama.attention.head_count_kv",
                     "qwen2.context_length", "llama.context_length",
                     "tokenizer.ggml.model", "tokenizer.ggml.tokens"]:
            val = self.metadata.get(key)
            if val is not None:
                display = val if not isinstance(val, list) else f"[list, len={len(val)}]"
                print(f"  {key} = {display}")

        # Tensor type summary
        type_counts = {}
        for ti in self.tensor_infos:
            tn = self._type_name(ti["type"])
            type_counts[tn] = type_counts.get(tn, 0) + 1
        print(f"\nTensor types:")
        for tn, count in sorted(type_counts.items()):
            print(f"  {tn}: {count}")

    def close(self):
        """Close the file."""
        if self.file:
            self.file.close()
            self.file = None
