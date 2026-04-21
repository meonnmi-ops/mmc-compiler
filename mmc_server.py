from flask import Flask, request, jsonify
from flask_cors import CORS
import subprocess
import os
import uuid
import tempfile

app = Flask(__name__)
CORS(app)  # IDE ကနေ ခေါ်လို့ရအောင် CORS ဖွင့်ပေးတာ

# MMC Compiler ရဲ့ လမ်းကြောင်း (ဆရာကြီးရဲ့ bin/mmc)
MMC_COMPILER = os.path.join(os.path.dirname(__file__), "bin", "mmc")

@app.route('/run', methods=['POST'])
def run_mmc():
    data = request.get_json()
    code = data.get('code', '')

    if not code:
        return jsonify({'error': 'Code is empty'}), 400

    # ယာယီဖိုင်တစ်ခု ဖန်တီးပြီး MMC ကုဒ်ကို သိမ်းမယ်
    with tempfile.NamedTemporaryFile(mode='w', suffix='.mmc', delete=False) as f:
        f.write(code)
        temp_mmc = f.name

    # Binary output အတွက် ယာယီဖိုင်အမည်
    temp_bin = tempfile.mktemp()

    try:
        # 1. Compile MMC -> Binary
        compile_cmd = [MMC_COMPILER, '-o', temp_bin, temp_mmc]
        compile_result = subprocess.run(compile_cmd, capture_output=True, text=True, timeout=10)

        if compile_result.returncode != 0:
            return jsonify({
                'error': 'Compilation failed',
                'details': compile_result.stderr
            }), 400

        # 2. Run Binary ကို Execute လုပ်မယ် (အချိန်ကန့်သတ်ချက်နဲ့)
        run_cmd = [temp_bin]
        run_result = subprocess.run(run_cmd, capture_output=True, text=True, timeout=5)

        output = run_result.stdout
        if run_result.stderr:
            output += "\n[stderr]\n" + run_result.stderr

        return jsonify({
            'output': output,
            'compile_stderr': compile_result.stderr
        })

    except subprocess.TimeoutExpired:
        return jsonify({'error': 'Execution timed out'}), 408
    except Exception as e:
        return jsonify({'error': str(e)}), 500
    finally:
        # ယာယီဖိုင်တွေ ရှင်းထုတ်မယ်
        if os.path.exists(temp_mmc):
            os.unlink(temp_mmc)
        if os.path.exists(temp_bin):
            os.unlink(temp_bin)

if __name__ == '__main__':
    app.run(host='127.0.0.1', port=5000, debug=True)
