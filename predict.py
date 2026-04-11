import sys
import joblib
import numpy as np

model = joblib.load("ai_model.joblib")

cpu = float(sys.argv[1])
mem = float(sys.argv[2])
disk = float(sys.argv[3])

X = np.array([[cpu, mem, disk]])

prediction = model.predict(X)[0]

print(prediction)