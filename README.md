# 🖥️ PredictiveAutoHeal

PredictiveAutoHeal is a Windows desktop monitoring and AI-assisted reliability analysis tool built using C++20, Win32 APIs, SQLite, and Python machine learning.

The application continuously monitors system health metrics and combines rule-based thresholds with AI predictions to estimate reliability risks in real time.

---

## ✨ Features

- 📊 Real-time CPU, memory, disk, and network monitoring
- 🧠 AI-powered reliability prediction system
- ⚠️ Rule-based anomaly and threshold detection
- 💾 SQLite-backed telemetry storage
- 🔄 Background metric batching pipeline
- 🖥️ Native Win32 dashboard
- ⚙️ Configurable performance and alert profiles
- 🚀 Portable deployment support
- 🧪 Python unit tests for prediction contracts

---

# 📸 Screenshots

```md
![Dashboard](screenshots/dashboard.png)
![AI Forecast](screenshots/ai-forecast.png)
![Metrics](screenshots/system-metrics.png)
![Decision Engine](screenshots/decision-engine.png)
```

---

# 🛠️ Tech Stack

## Core Application
- C++20
- Win32 API
- SQLite
- CMake

## AI / ML Layer
- Python 3.12+
- NumPy
- Pandas
- Scikit-learn
- Joblib

---

# 📁 Project Structure

```text
PredictiveAutoHeal/
│
├── main.cpp                 # Dashboard and runtime integration
├── SystemMetrics.*          # System metric collection
├── MetricsStorage.*         # SQLite schema and logging
├── MetricsPipeline.*        # Background batching pipeline
├── DecisionEngine.*         # Risk scoring and recommendations
├── AppConfig.*              # Config parsing
│
├── train_model.py           # Model training
├── predict_model.py         # Runtime prediction
├── model_features.py        # Feature engineering
├── labeling.py              # Label generation
├── test_model_contract.py   # Model contract tests
│
├── portable/                # Portable runtime bundle
├── TRANSFER.md              # Deployment guide
├── requirements.txt
└── README.md
```

---

# ⚡ Requirements

- Windows
- Visual Studio 2022 Build Tools
- CMake 3.20+
- Python 3.12+

SQLite is bundled directly in the repository.

---

# 🚀 Getting Started

## 1️⃣ Clone the Repository

```powershell
git clone https://github.com/your-username/PredictiveAutoHeal.git
cd PredictiveAutoHeal
```

---

## 2️⃣ Install Python Dependencies

```powershell
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

If Python is not available globally, update `config.txt`:

```txt
PYTHON_EXE=C:\Path\To\python.exe
```

---

## 3️⃣ Build The Project

Open a Developer PowerShell:

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && powershell'
```

Configure and build:

```powershell
mkdir build
cd build

cmake ..
cmake --build . --config Debug
```

Executable location:

```text
build\Debug\PredictiveAutoHeal.exe
```

---

# ▶️ Run The Application

```powershell
.\build\Debug\PredictiveAutoHeal.exe
```

The application automatically creates:

```text
monitor.db
```

for storing telemetry data locally.

---

# 🧠 Train The AI Model

Run the application first so enough telemetry data is collected.

Then train the model:

```powershell
python train_model.py --db build\monitor.db --model build\ai_model.joblib --meta build\ai_model_meta.json
```

Generated reports:

```text
model_report.json
model_report.txt
```

---

# 🧪 Run Tests

```powershell
python -m unittest test_model_contract.py
```

---

# ⚙️ Configuration

Runtime settings are stored inside:

```text
config.txt
```

Example:

```txt
CPU_THRESHOLD=80
MEM_THRESHOLD=85
DISK_THRESHOLD=8
AI_ALERT_THRESHOLD=70
SAFE_MODE=1
SERVICE_NAME=Spooler
```

---

# 🎯 Performance Modes

| Mode | Description |
|---|---|
| `LOW_END` | Reduced prediction frequency |
| `BALANCED` | Moderate prediction frequency |
| `HIGH_PERFORMANCE` | Aggressive AI prediction checks |

---

# 📦 Portable Deployment

The project supports portable deployment.

Required files:

```text
PredictiveAutoHeal.exe
config.txt
predict_model.py
model_features.py
labeling.py
ai_model.joblib
ai_model_meta.json
requirements.txt
```

Run:

```powershell
.\PredictiveAutoHeal.exe
```

---

# 🔒 Safety Notes

- All telemetry processing is local
- No external cloud dependency
- AI prediction failures automatically fall back to rule-based monitoring
- `SAFE_MODE=1` keeps recovery actions conservative

---

# 🌟 Future Improvements

- GPU monitoring support
- Live performance graphs
- Automated recovery actions
- Deep-learning anomaly detection
- Web dashboard integration
- Multi-device monitoring

---

# 🤝 Contributing

Contributions and improvements are welcome.

Feel free to:
- Fork the repository
- Open issues
- Submit pull requests

---

# 📄 License


---

# ⭐ Support

If you found this project useful, consider giving it a star on GitHub!
