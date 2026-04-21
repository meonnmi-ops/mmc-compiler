# MMC (Myanmar Code) Transpiler v8.1

**ကမ္ဘာ့ပထမ မြန်မာဘာသာစကား ပရောဂျက်ဖွင့်ဆောင်ရေးစက် (World's First Myanmar Programming Language Transpiler)**

MMC (Myanmar Code) သည် မြန်မာဘာသာစကားဖြင့်ရေးသားနိုင်သော ပရောဂျက်ဖွင့်ဆောင်ရေးစက်ဖြစ်ပြီး Python ဘာသာစကားသို့ ချိတ်ဆွဲပေးခြင်းဖြင့် အလုပ်လုပ်သည်။ v8.1 တွင် ကိန်းချိန်စား 181 ခု (keywords) ပါဝင်ပြီး OOP, Dict, Error Handling, File I/O, Async, Decorators, Type Hints တို့ကို ကိုယ်စီမံဖွင့်ပေးသည်။

## ✨ ထူးခြားချက်များ

- **181 Keywords** - မြန်မာစကားဖြင့် ပညာရေးရှင်မရှိပါကလည်း ဘာသာစကားတွေကို နားလည်နိုင်ရန်
- **OOP ထောင့်ပါ** - Class, Inheritance, Super, isinstance စသည်
- **File I/O** - ဖိုင်ဖွင့်/ဖတ်/ရေးပါ
- **Error Handling** - try/except/finally ကို မြန်မာနဲ့ ရေးလို့ရတယ်
- **Type Hints** - ဆန်ခြေမြှင့်တင်ပေးနိုင်ပါသည်
- **Async/Await** - မတူညီ/မျှော် ဖြင့် async programming လုပ်နိုင်ပါသည်
- **Termux ရုံ** - Android မှာ Python 3.8+ ဖြင့် အလုပ်လုပ်ပါသည်

## 📦 အသစ်တင်ထည့်သောအတိုင်း (v8.1)

- `စာရင်း` List Comprehension ထောင့်ပါ
- Decorator Syntax ထောင့်ပါ (`@ decorator`)
- Lambda `လံဘ်ဒါ` ထောင့်ပါ
- `မှန်လျှင်` (inline if/else ternary)
- `အတူ` (with/context manager) ထောင့်ပါ

## 🚀 သုံးလုပ်ပုံ

### 1. Clone လုပ်ပါ

```bash
git clone https://github.com/meonnmi-ops/mmc-compiler.git
cd mmc-compiler
```

### 2. Termux တွင် အသစ်တင်သွင်းပါ

```bash
pkg install python git
git clone https://github.com/meonnmi-ops/mmc-compiler.git
cd mmc-compiler
```

### 3. MMC ကုဒ် ရေးပြီး Python သို့ ပြောင်းပါ

```bash
python3 mmc_transpiler.py examples/hello.mmc
```

### 4. ရှေးတွင်ပါ ရေးပြီး ပြောင်းနိုင်ပါသည်

```bash
python3 mmc_transpiler.py -c 'ပုံနှိပ်("မကြီးမားသော ကမ္ဘာ")'
```

### 5. Output ကို Python ဖြင့် အခုပါ လုပ်ဆောင်နိုင်ပါသည်

```bash
python3 mmc_transpiler.py examples/hello.mmc | python3
```

## 📖 ကုဒ် ကိုယ်ပိုင် ဥပမာများ

### Hello World

```mmc
ပုံနှိပ်("မကြီးမားသော ကမ္ဘာ")
```

### ကိန်း သတ်မှတ်ခြင်း

```mmc
ကိန်း အမျိုး = ၅
ကိန်း အမြဲ = "မြန်မာ"
ပုံနှိပ်(အမျိုး, အမြဲ)
```

### အတွက် လမ်းကြောင်း (For Loop)

```mmc
အတွက် ခရီး ပတ်လမ်း(၅):
    ပုံနှိပ်(ခရီး)
```

### အကယ်၍ / မဟုတ်ပါက (If / Else)

```mmc
ကိန်း အရေးကြီးမှု = ၉၀

အကယ်၍ အရေးကြီးမှု >= ၈၀:
    ပုံနှိပ်("အရေးကြီးပါသည်")
မဟုတ်ပါက:
    ပုံနှိပ်("ရှေးမှ ထပ်လုပ်ပါ")
```

### အတန်း (Class - OOP)

```mmc
အတန်း ခေါ်ဆိုချက်:
    အဓိပ္ပာယ် စနစ်(ခွဲ, အမည်):
        ခွဲ.ခွဲ = ခွဲ
        ခွဲ.အမည် = အမည်

    အဓိပ္ပာယ် ကြက်ခြေ():
        ပြန်ပေး f"{ခွဲ.အမည်} ၏ ခွဲမှာ {ခွဲ.ခွဲ} ရှိသည်"

ကိန်း ပရောဂျက် = ခေါ်ဆိုချက်("MMC", "မြန်မာ Code")
ပုံနှိပ်(ပရောဂျက်.ကြက်ခြေ())
```

### ဖိုင် ဖွင့်/ဖတ်/ရေး (File I/O)

```mmc
အတူ ဖိုင် = ဖွင့်("data.txt", "w"):
    ဖိုင်.ဖိုင်ရေး("Hello MMC")

ကိန်း အချက် = ဖွင့်("data.txt").ဖိုင်ဖတ်()
ပုံနှိပ်(အချက်)
```

### ဖမ်းမိ (Try/Except)

```mmc
ကြိုးစား:
    ကိန်း အမျိုး = ၁၀ / ၀
ဖမ်းမိ ပွဲဖွင့် ဖြစ်ပွဲ:
    ပုံနှိပ်("ပွဲဖွင့်: ", ဖြစ်ပွဲ)
```

### စာရင်း Comprehension (v8.1)

```mmc
ကိန်း ထုတ်ကုန် = [x * x အတွက် x ပတ်လမ်း(၁၀)]
ပုံနှိပ်(ထုတ်ကုန်)
```

## 🔑 စကားလုံးအုတ် (Keyword Categories)

| မြောက်ခံ (Category) | ဥပမာ Keywords | Python ပြောင်းချက် |
|---|---|---|
| Statement | `အကယ်၍`, `အတွက်`, `ပြန်ပေး` | `if`, `for`, `return` |
| Function | `ပုံနှိပ်`, `အရှည်`, `ပတ်လမ်း` | `print`, `len`, `range` |
| Method | `ထည့်သွင်း`, `ဖိုင်ဖတ်`, `စာခွဲ` | `append`, `read`, `split` |
| Value | `မှန်`, `မှား`, `ဘာမှမရှိ` | `True`, `False`, `None` |
| Type | `အတည်`, `ဒစ်`, `စာသား` | `int`, `float`, `str` |

## 🛠️ နည်းပညာရပ် ဆုံးဖြတ်ချက်

- **ဘာသာစကား** - Python 3.8+
- **မှုန်လိုက်** - Termux (Android), Linux, macOS, Windows
- **ကိန်းချိန်** - Python ပိုမိုတင်ရပါမယ်

## 📄 လိုက်စားစီမံချက်

ဤ ပရောဂျက်ကို MIT License ဖြင့် ထုတ်ပေးထားပါသည်။ အချင်းခင်း အသုံးပြုနိုင်ပါသည်။

---

**MMC v8.1** - *မြန်မာဘာသာစကားဖြင့် ပရောဂျက်ရေးဆောင်ပါ - ကမ္ဘာကို ပြောတွေ့ပါ!*
