#!/usr/bin/env python3
"""
NYANLIN AI - Pure Python GGUF Inference Engine
Version 2.0 (All bugs fixed)

Usage:
    python main.py <model.gguf> [options]

Options:
    --prompt "Your text here"   Custom prompt
    --max-tokens 128           Max tokens to generate
    --temperature 0.8          Sampling temperature
    --top-k 40                 Top-K filtering
    --top-p 0.95               Top-P (nucleus) filtering
"""

import sys
import os

# Add parent directory to path so we can import nyanlin package
# This works whether you run: python main.py  (from inside nyanlin/)
#                   or:    python nyanlin/main.py  (from parent dir)
_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _dir)           # for: python nyanlin/main.py
sys.path.insert(0, os.path.dirname(_dir))  # for: cd nyanlin && python main.py

from nyanlin.inference.generator import Generator


def main():
    if len(sys.argv) < 2:
        print("NYANLIN AI - Pure Python GGUF Inference Engine v2.0")
        print()
        print("Usage: python main.py <model.gguf> [options]")
        print()
        print("Options:")
        print("  --prompt \"text\"    Custom prompt (default: interactive chat)")
        print("  --max-tokens N     Max tokens to generate (default: 256)")
        print("  --temperature F    Sampling temperature (default: 0.8)")
        print("  --top-k N          Top-K filtering (default: 40)")
        print("  --top-p F          Top-P nucleus filtering (default: 0.95)")
        print()
        print("Example:")
        print("  python main.py qwen2.5-0.5b-instruct-fp16.gguf")
        print("  python main.py model.gguf --prompt \"Hello, how are you?\"")
        sys.exit(1)

    # Parse arguments
    model_path = sys.argv[1]
    prompt = None
    max_tokens = 256
    temperature = 0.8
    top_k = 40
    top_p = 0.95

    i = 2
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg == "--prompt" and i + 1 < len(sys.argv):
            prompt = sys.argv[i + 1]
            i += 2
        elif arg == "--max-tokens" and i + 1 < len(sys.argv):
            max_tokens = int(sys.argv[i + 1])
            i += 2
        elif arg == "--temperature" and i + 1 < len(sys.argv):
            temperature = float(sys.argv[i + 1])
            i += 2
        elif arg == "--top-k" and i + 1 < len(sys.argv):
            top_k = int(sys.argv[i + 1])
            i += 2
        elif arg == "--top-p" and i + 1 < len(sys.argv):
            top_p = float(sys.argv[i + 1])
            i += 2
        else:
            i += 1

    # Check model file exists
    if not os.path.isfile(model_path):
        print(f"Error: Model file not found: {model_path}")
        sys.exit(1)

    # Build generator
    gen = Generator(model_path=model_path)

    try:
        # Build (parse GGUF headers + metadata)
        gen.build()

        # Load weights (slow for large models)
        gen.load()

        # Interactive or single prompt mode
        if prompt:
            # Single generation
            gen.generate(prompt, max_tokens=max_tokens, temperature=temperature,
                        top_k=top_k, top_p=top_p)
        else:
            # Interactive chat mode
            print("\n=== NYANLIN Chat Mode ===")
            print("Type your message and press Enter.")
            print("Type 'quit' or 'exit' to stop.")
            print()

            # Qwen-style chat template
            system_msg = "You are a helpful assistant."

            messages = [{"role": "system", "content": system_msg}]

            while True:
                try:
                    user_input = input("You> ").strip()
                except (EOFError, KeyboardInterrupt):
                    print("\nGoodbye!")
                    break

                if not user_input:
                    continue
                if user_input.lower() in ("quit", "exit"):
                    print("Goodbye!")
                    break

                messages.append({"role": "user", "content": user_input})

                # Format prompt with chat template
                formatted = format_chat(messages, gen.arch)
                print(f"\nBot> ", end="", flush=True)

                response = gen.generate(
                    formatted,
                    max_tokens=max_tokens,
                    temperature=temperature,
                    top_k=top_k,
                    top_p=top_p,
                    stream=True
                )

                messages.append({"role": "assistant", "content": response})
                print()

    except Exception as e:
        print(f"\nError: {type(e).__name__}: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        gen.close()


def format_chat(messages, arch="qwen2"):
    """Format messages into a prompt string using appropriate chat template."""
    if arch in ("qwen2", "qwen", "mistral", "llama"):
        # Qwen2 chat template
        parts = []
        for msg in messages:
            role = msg["role"]
            content = msg["content"]
            if role == "system":
                parts.append(f"<|im_start|>system\n{content}<|im_end|>")
            elif role == "user":
                parts.append(f"<|im_start|>user\n{content}<|im_end|>")
            elif role == "assistant":
                parts.append(f"<|im_start|>assistant\n{content}<|im_end|>")
        parts.append("<|im_start|>assistant\n")
        return "".join(parts)
    else:
        # Simple format
        parts = []
        for msg in messages:
            parts.append(f"{msg['role']}: {msg['content']}")
        parts.append("assistant: ")
        return "\n".join(parts)


if __name__ == "__main__":
    main()
