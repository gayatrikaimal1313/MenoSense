from fastapi import FastAPI, Request
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from pydantic import BaseModel
from typing import List, Optional
import time
import uvicorn
import math
def voltage_to_conductance(voltage: float) -> float:
    table = [
        (0.639, 0.455),
        (0.655, 0.213),
        (0.662, 0.100),
    ]

    table = sorted(table)

    if voltage <= table[0][0]:
        return table[0][1]
    if voltage >= table[-1][0]:
        return table[-1][1]

    for i in range(len(table) - 1):
        v1, g1 = table[i]
        v2, g2 = table[i + 1]

        if v1 <= voltage <= v2:
            return g1 + (voltage - v1) * (g2 - g1) / (v2 - v1)

    return None
app = FastAPI()

class SensorData(BaseModel):
    conductance: Optional[float] = None
    voltage: Optional[float] = None
    adc: Optional[int] = None
    temperature: float
    humidity: float
    batteryVoltage: float
    wifiConnected: bool
    edaAvailable: bool
    sensorStatus: str  # "temperature-only", "full-sensor", "no-data"

data_history = []
max_history = 1000
calibration_params = {"offset": 0.0, "multiplier": 1.0}
last_post_timestamp = 0
event_count = 0
last_event_state = False

class CalibrationData(BaseModel):
    offset: float
    multiplier: float

@app.post("/api/calibrate")
async def set_calibration(data: CalibrationData):
    global calibration_params
    calibration_params = data.dict()
    return {"status": "success", "params": calibration_params}

@app.get("/api/calibrate")
async def get_calibration():
    return calibration_params

@app.post("/api/data")
async def receive_data(data: SensorData):
    global last_post_timestamp

    entry = data.dict()

    # Validate signal quality
    entry["edaValid"] = (
        entry.get("edaAvailable") is True
        and entry.get("voltage") is not None
        and 0.05 < entry["voltage"] < 1.05
    )

    entry["tempValid"] = 10 < entry["temperature"] < 60

    # Backend calibration from voltage
    if entry["edaValid"]:
        raw_g = voltage_to_conductance(entry["voltage"])
        entry["conductance"] = (
            raw_g * calibration_params.get("multiplier", 1.0)
        ) + calibration_params.get("offset", 0.0)
    else:
        entry["conductance"] = None

    entry["timestamp"] = time.time()
    entry["eventDetected"] = False

    last_post_timestamp = entry["timestamp"]

    data_history.append(entry)

    if len(data_history) > max_history:
        data_history.pop(0)

    return {"status": "success"}

def run_inference(history):
    global event_count, last_event_state

    if not history:
        return {"state": "Waiting for ESP", "source": "None", "confidence": "Low"}

    if len(history) < 15:
        return {"state": "Initializing", "source": "None", "confidence": "Low"}

    latest = history[-1]
    recent = history[-15:]

    temps = [h["temperature"] for h in recent if h.get("tempValid", True)]
    eda_values = [h["conductance"] for h in recent if h.get("conductance") is not None]

    temp_rise = temps[-1] - temps[0] if len(temps) >= 2 else 0
    eda_rise = eda_values[-1] - eda_values[0] if len(eda_values) >= 2 else 0

    score = 0
    sources = []

    if eda_rise > 0.3:
        score += 2
        sources.append("EDA Rise")

    if temp_rise > 0.4:
        score += 1
        sources.append("Temperature Rise")

    if latest.get("humidity", 0) > 50:
        score += 0.5
        sources.append("Humidity Context")

    detected = score >= 2.5
    possible = 1.5 <= score < 2.5

    if detected:
        latest["eventDetected"] = True

        if not last_event_state:
            event_count += 1

        last_event_state = True

        return {
            "state": "Hot Flash Detected",
            "source": " + ".join(sources),
            "confidence": "High",
            "score": score
        }

    last_event_state = False

    if possible:
        return {
            "state": "Possible Event",
            "source": " + ".join(sources),
            "confidence": "Moderate",
            "score": score
        }

    return {
        "state": "Stable",
        "source": "Hardware Data",
        "confidence": "Low",
        "score": score
    }

def generate_insights(history):
    if len(history) < 10:
        return [
            "Collecting data. Insights will appear after more sensor samples.",
            "Temperature and humidity monitoring are active.",
            "EDA insight requires valid conductance readings."
        ]

    latest = history[-1]
    recent = history[-30:]

    insights = []

    temps = [h["temperature"] for h in recent if h.get("tempValid", True)]
    humidities = [h["humidity"] for h in recent if h.get("humidity") is not None]
    eda_values = [h["conductance"] for h in recent if h.get("conductance") is not None]

    if temps:
        avg_temp = sum(temps) / len(temps)
        insights.append(f"Recent average temperature is {avg_temp:.1f} °C.")

    if humidities:
        avg_humidity = sum(humidities) / len(humidities)
        insights.append(f"Recent average humidity is {avg_humidity:.0f}%.")

    if eda_values:
        eda_change = eda_values[-1] - eda_values[0]
        if eda_change > 0.3:
            insights.append("EDA has increased over the recent window.")
        elif eda_change < -0.3:
            insights.append("EDA has decreased over the recent window.")
        else:
            insights.append("EDA is relatively stable over the recent window.")
    else:
        insights.append("EDA data is currently unavailable; using temperature-only monitoring.")

    if latest.get("eventDetected"):
        insights.append("A hot flash pattern was detected in the latest sensor window.")
    else:
        insights.append("No hot flash pattern detected in the latest sensor window.")

    return insights

@app.get("/api/data")
async def get_data():
    inference = run_inference(data_history)

    return {
    "history": data_history,
    "inference": inference,
    "lastSync": last_post_timestamp,
    "deviceConnected": (time.time() - last_post_timestamp) < 10 if last_post_timestamp > 0 else False,
    "eventCount": event_count,
    "sampleCount": len(data_history),
    "insights": generate_insights(data_history)
}

app.mount("/static", StaticFiles(directory="backend/static"), name="static")

@app.get("/")
async def root():
    return FileResponse("backend/static/index.html")

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)

