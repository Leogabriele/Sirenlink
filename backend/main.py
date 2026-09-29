import os
import math
import json
from datetime import datetime
from typing import Dict
from fastapi import FastAPI, Depends, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from sqlalchemy.orm import Session
from sqlalchemy import text

from database import get_db, engine
import models
import schemas
import auth

app = FastAPI(title="SirenLink Backend API")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ─────────────────────────────────────────────────────────────
# Shared In-Memory Simulation State
# This is the single source of truth for all three dashboards.
# ─────────────────────────────────────────────────────────────
# ─────────────────────────────────────────────────────────────
# Shared In-Memory Simulation State
# ─────────────────────────────────────────────────────────────
sim_state: dict = {
    "ambulance_id":          "AMB-A01",
    "ambulance_lat":         None,
    "ambulance_lng":         None,
    "ambulance_speed":       40.0,
    "ambulance_heading":     0.0,
    "hospital_id":           "HSP-2201",
    "hospital_name":         "City General Hospital",
    "hospital_lat":          None,
    "hospital_lng":          None,
    # Legacy single-vehicle fields (kept for backward compat)
    "vehicle_id":            "VEH-V01",
    "vehicle_lat":           None,
    "vehicle_lng":           None,
    "emergency_active":      False,
    "emergency_start_time":  None,
    "distance_to_hospital":  None,
    "distance_to_vehicle":   None,
    "eta_seconds":           None,
    "vehicle_instruction":   "STANDBY",
    "vehicles_alerted":      0,
    "arrived":               False,
    "vehicle_moved":         False,
}

# Multi-vehicle registry  { identifier -> {"lat":..,"lng":..,"instruction":..,"dist":..,"moved":bool} }
vehicle_registry: dict = {}
# Track which hospital distance milestones have been broadcast (100m, 500m)
hospital_alerts_sent: set = set()

# ─────────────────────────────────────────────────────────────
# WebSocket Connection Manager
# ─────────────────────────────────────────────────────────────
connections: Dict[str, Dict[str, WebSocket]] = {
    "hospital":  {},
    "ambulance": {},
    "vehicle":   {},
}

async def broadcast(message_dict: dict):
    """Sends a message to ALL connected clients across all roles."""
    for role, clients in connections.items():
        for identifier, ws in list(clients.items()):
            try:
                await ws.send_json(message_dict)
            except Exception:
                pass

async def broadcast_to_vehicles(message_dict: dict):
    """Sends a targeted message to all vehicle clients."""
    for identifier, ws in list(connections["vehicle"].items()):
        try:
            await ws.send_json(message_dict)
        except Exception:
            pass

async def send_to_role(role: str, message_dict: dict):
    """Sends a message to all clients of a specific role."""
    if role in connections:
        for identifier, ws in list(connections[role].items()):
            try:
                await ws.send_json(message_dict)
            except Exception:
                pass

async def send_to_client(role: str, identifier: str, message_dict: dict):
    """Sends a message to one specific client."""
    if role in connections and identifier in connections[role]:
        try:
            await connections[role][identifier].send_json(message_dict)
        except Exception:
            pass

# ─────────────────────────────────────────────────────────────
# Geo Math Utilities
# ─────────────────────────────────────────────────────────────
def haversine_distance(lat1, lon1, lat2, lon2) -> float:
    R = 6371000
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2.0) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2.0) ** 2
    return R * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))

def calculate_bearing(lat1, lon1, lat2, lon2) -> float:
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dlambda = math.radians(lon2 - lon1)
    y = math.sin(dlambda) * math.cos(phi2)
    x = math.cos(phi1) * math.sin(phi2) - math.sin(phi1) * math.cos(phi2) * math.cos(dlambda)
    return (math.degrees(math.atan2(y, x)) + 360) % 360

def get_direction(ambulance_heading: float, bearing_to_vehicle: float) -> str:
    diff = (bearing_to_vehicle - ambulance_heading + 360) % 360
    if diff < 45 or diff > 315:
        return "PULL_OVER"
    elif 45 <= diff < 180:
        return "RIGHT"
    elif 180 <= diff < 315:
        return "LEFT"
    return "HOLD"

# ─────────────────────────────────────────────────────────────
# Per-Vehicle Instruction Engine (multi-vehicle)
# ─────────────────────────────────────────────────────────────
def compute_instruction_for_vehicle(vlat: float, vlng: float) -> tuple:
    """Returns (instruction, dist) for a given vehicle position."""
    if not sim_state["emergency_active"]:
        return ("STANDBY", None)
    alat = sim_state["ambulance_lat"]
    alng = sim_state["ambulance_lng"]
    if None in (alat, alng, vlat, vlng):
        return ("STANDBY", None)
    dist = haversine_distance(alat, alng, vlat, vlng)
    if dist > 500:
        return ("STANDBY", dist)
    if dist > 50:
        bearing = calculate_bearing(alat, alng, vlat, vlng)
        direction = get_direction(sim_state["ambulance_heading"], bearing)
        if direction == "RIGHT":
            return ("MOVE_LEFT", dist)
        elif direction == "LEFT":
            return ("MOVE_RIGHT", dist)
        else:
            return ("MOVE_LEFT", dist)
    return ("AMBULANCE_PASSING", dist)

def update_computed_state():
    """Recalculate all derived values whenever positions change."""
    alat = sim_state["ambulance_lat"]
    alng = sim_state["ambulance_lng"]
    hlat = sim_state["hospital_lat"]
    hlng = sim_state["hospital_lng"]

    if None not in (alat, alng, hlat, hlng):
        sim_state["distance_to_hospital"] = haversine_distance(alat, alng, hlat, hlng)
        speed_ms = sim_state["ambulance_speed"] * 1000 / 3600
        sim_state["eta_seconds"] = (
            sim_state["distance_to_hospital"] / speed_ms if speed_ms > 0 else None
        )
    else:
        sim_state["distance_to_hospital"] = None
        sim_state["eta_seconds"] = None

    # Update multi-vehicle registry
    alerted = 0
    for vid, vdata in vehicle_registry.items():
        instr, dist = compute_instruction_for_vehicle(vdata["lat"], vdata["lng"])
        vdata["instruction"] = instr
        vdata["dist"] = dist
        if instr not in ("STANDBY", "ROAD_CLEARED"):
            alerted += 1

    sim_state["vehicles_alerted"] = alerted

    # Legacy single vehicle fallback
    vlat = sim_state["vehicle_lat"]
    vlng = sim_state["vehicle_lng"]
    if None not in (alat, alng, vlat, vlng):
        instr, dist = compute_instruction_for_vehicle(vlat, vlng)
        sim_state["vehicle_instruction"] = instr
        sim_state["distance_to_vehicle"] = dist
    else:
        sim_state["vehicle_instruction"] = "STANDBY"

# ─────────────────────────────────────────────────────────────
# WebSocket Message Handler
# ─────────────────────────────────────────────────────────────
async def handle_ws_message(role: str, identifier: str, msg: dict):
    t = msg.get("type")

    # ── Ambulance reports its new position ────────────────────
    if t == "location_update" and role == "ambulance":
        sim_state["ambulance_lat"]     = msg.get("lat")
        sim_state["ambulance_lng"]     = msg.get("lng")
        sim_state["ambulance_heading"] = msg.get("heading", 0)
        sim_state["ambulance_speed"]   = msg.get("speed", 40)
        update_computed_state()

        # Arrival detection: ambulance within 30 m of hospital
        dist_h = sim_state["distance_to_hospital"]
        if (
            dist_h is not None
            and dist_h < 30
            and not sim_state["arrived"]
            and sim_state["emergency_active"]
        ):
            sim_state["arrived"]          = True
            sim_state["emergency_active"] = False
            sim_state["vehicle_instruction"] = "ROAD_CLEARED"
            for vdata in vehicle_registry.values():
                vdata["instruction"] = "ROAD_CLEARED"
            hospital_alerts_sent.clear()
            await broadcast({"type": "ambulance_arrived", **sim_state,
                             "vehicles": _vehicle_list()})
            return

        # Hospital proximity milestone alerts (500m and 100m)
        if dist_h is not None and sim_state["emergency_active"]:
            if dist_h < 500 and "500m" not in hospital_alerts_sent:
                hospital_alerts_sent.add("500m")
                await send_to_role("hospital", {
                    "type": "hospital_proximity_alert",
                    "milestone": "500m",
                    "distance": dist_h,
                    "eta_seconds": sim_state["eta_seconds"],
                    **sim_state
                })
            if dist_h < 100 and "100m" not in hospital_alerts_sent:
                hospital_alerts_sent.add("100m")
                await send_to_role("hospital", {
                    "type": "hospital_proximity_alert",
                    "milestone": "100m",
                    "distance": dist_h,
                    "eta_seconds": sim_state["eta_seconds"],
                    **sim_state
                })

        # Per-vehicle targeted instructions
        for vid, vdata in vehicle_registry.items():
            await send_to_client("vehicle", vid, {
                "type": "state_update",
                "for_vehicle": vid,
                "vehicle_instruction": vdata["instruction"],
                "distance_to_vehicle": vdata["dist"],
                **sim_state,
                "vehicles": _vehicle_list()
            })

        # Broadcast general state to hospitals + ambulances
        await send_to_role("hospital", {"type": "state_update", **sim_state, "vehicles": _vehicle_list()})
        await send_to_role("ambulance", {"type": "state_update", **sim_state, "vehicles": _vehicle_list()})

    # ── Ambulance sets hospital location on map ───────────────
    elif t == "hospital_location":
        sim_state["hospital_lat"]  = msg.get("lat")
        sim_state["hospital_lng"]  = msg.get("lng")
        sim_state["hospital_name"] = msg.get("name", sim_state["hospital_name"])
        update_computed_state()
        await broadcast({"type": "state_update", **sim_state, "vehicles": _vehicle_list()})

    # ── Vehicle registers / updates its position ──────────────
    elif t == "vehicle_location":
        vlat = msg.get("lat")
        vlng = msg.get("lng")
        vid = msg.get("vehicle_id", identifier)
        if vlat is not None:
            if vid not in vehicle_registry:
                vehicle_registry[vid] = {"lat": vlat, "lng": vlng,
                                         "instruction": "STANDBY",
                                         "dist": None, "moved": False}
            else:
                vehicle_registry[vid]["lat"] = vlat
                vehicle_registry[vid]["lng"] = vlng
            # Legacy single-vehicle fallback
            sim_state["vehicle_lat"] = vlat
            sim_state["vehicle_lng"] = vlng
        update_computed_state()
        await broadcast({"type": "state_update", **sim_state, "vehicles": _vehicle_list()})

    # ── Ambulance activates emergency ─────────────────────────
    elif t == "emergency_activate":
        sim_state["emergency_active"]     = True
        sim_state["arrived"]              = False
        sim_state["vehicles_alerted"]     = 0
        sim_state["vehicle_moved"]        = False
        sim_state["emergency_start_time"] = datetime.utcnow().isoformat()
        hospital_alerts_sent.clear()
        for vdata in vehicle_registry.values():
            vdata["moved"] = False
            vdata["instruction"] = "STANDBY"
        if msg.get("lat") is not None:
            sim_state["ambulance_lat"] = msg["lat"]
        if msg.get("lng") is not None:
            sim_state["ambulance_lng"] = msg["lng"]
        if msg.get("hospital_lat") is not None:
            sim_state["hospital_lat"] = msg["hospital_lat"]
        if msg.get("hospital_lng") is not None:
            sim_state["hospital_lng"] = msg["hospital_lng"]
        update_computed_state()
        await broadcast({"type": "emergency_activated", "ambulance_id": identifier,
                         **sim_state, "vehicles": _vehicle_list()})

    # ── Ambulance deactivates emergency ───────────────────────
    elif t == "emergency_deactivate":
        sim_state["emergency_active"]    = False
        sim_state["vehicle_moved"]       = False
        sim_state["vehicle_instruction"] = "STANDBY"
        sim_state["vehicles_alerted"]    = 0
        hospital_alerts_sent.clear()
        for vdata in vehicle_registry.values():
            vdata["instruction"] = "STANDBY"
        await broadcast({"type": "emergency_deactivated", "ambulance_id": identifier,
                         **sim_state, "vehicles": _vehicle_list()})

    # ── Road-cleared signal ───────────────────────────────────
    elif t == "road_cleared":
        sim_state["vehicle_instruction"] = "ROAD_CLEARED"
        sim_state["vehicle_moved"]       = False
        await broadcast({"type": "state_update", **sim_state, "vehicles": _vehicle_list()})

    # ── Vehicle moved / given way feedback ────────────────────
    elif t == "vehicle_moved":
        sim_state["vehicle_moved"] = True
        if identifier in vehicle_registry:
            vehicle_registry[identifier]["moved"] = True
        await broadcast({"type": "vehicle_gave_way", "vehicle_id": identifier,
                         **sim_state, "vehicles": _vehicle_list()})

    # ── Keepalive / ping ──────────────────────────────────────
    elif t == "ping":
        pass

def _vehicle_list():
    """Returns vehicle registry as a serializable list for broadcasting."""
    return [
        {"id": vid, "lat": v["lat"], "lng": v["lng"],
         "instruction": v["instruction"], "dist": v["dist"], "moved": v["moved"]}
        for vid, v in vehicle_registry.items()
    ]


# ─────────────────────────────────────────────────────────────
# WebSocket Endpoint  (bidirectional)
# ─────────────────────────────────────────────────────────────
@app.websocket("/ws/{role}/{identifier}")
async def websocket_endpoint(websocket: WebSocket, role: str, identifier: str):
    await websocket.accept()
    if role not in connections:
        connections[role] = {}
    connections[role][identifier] = websocket

    # Immediately sync new client with full state + vehicle list
    await websocket.send_json({"type": "state_sync", **sim_state, "vehicles": _vehicle_list()})

    try:
        while True:
            raw = await websocket.receive_text()
            try:
                msg = json.loads(raw)
                await handle_ws_message(role, identifier, msg)
            except json.JSONDecodeError:
                pass
            except Exception as exc:
                print(f"[WS handler error] {exc}")
    except WebSocketDisconnect:
        if role in connections and identifier in connections[role]:
            del connections[role][identifier]
        # Remove from vehicle registry on disconnect
        if role == "vehicle" and identifier in vehicle_registry:
            del vehicle_registry[identifier]

# ─────────────────────────────────────────────────────────────
# Simulation-State REST Endpoints
# ─────────────────────────────────────────────────────────────
@app.get("/api/state")
def get_sim_state():
    """Returns the full current simulation state."""
    return {**sim_state, "vehicles": _vehicle_list()}

@app.post("/api/state/reset")
async def reset_sim_state():
    """Resets simulation to blank slate."""
    sim_state.update({
        "ambulance_lat":         None,
        "ambulance_lng":         None,
        "ambulance_speed":       40.0,
        "ambulance_heading":     0.0,
        "hospital_lat":          None,
        "hospital_lng":          None,
        "vehicle_lat":           None,
        "vehicle_lng":           None,
        "emergency_active":      False,
        "emergency_start_time":  None,
        "distance_to_hospital":  None,
        "distance_to_vehicle":   None,
        "eta_seconds":           None,
        "vehicle_instruction":   "STANDBY",
        "vehicles_alerted":      0,
        "arrived":               False,
        "vehicle_moved":         False,
    })
    vehicle_registry.clear()
    hospital_alerts_sent.clear()
    await broadcast({"type": "state_reset", **sim_state, "vehicles": []})
    return {"message": "Simulation state reset"}


# ─────────────────────────────────────────────────────────────
# Existing Auth / Emergency / Location REST Endpoints
# (preserved exactly as-is)
# ─────────────────────────────────────────────────────────────
@app.on_event("startup")
def startup_event():
    db = next(get_db())
    hosp = db.query(models.Hospital).filter(models.Hospital.hospital_id == "HSP-2201").first()
    if not hosp:
        hosp = models.Hospital(
            hospital_id="HSP-2201",
            name="City General Hospital",
            password_hash=auth.hash_password("demo123"),
        )
        db.add(hosp)
    amb = db.query(models.Ambulance).filter(models.Ambulance.vehicle_number == "AMB-A01").first()
    if not amb:
        amb = models.Ambulance(vehicle_number="AMB-A01", hospital_id="HSP-2201")
        db.add(amb)
    veh = db.query(models.Vehicle).filter(models.Vehicle.vehicle_number == "VEH-V01").first()
    if not veh:
        veh = models.Vehicle(vehicle_number="VEH-V01")
        db.add(veh)
    db.commit()

@app.post("/api/auth/register")
def register_hospital(req: schemas.HospitalRegister, db: Session = Depends(get_db)):
    if db.query(models.Hospital).filter(models.Hospital.hospital_id == req.hospital_id).first():
        raise HTTPException(status_code=400, detail="Hospital already registered")
    new_hosp = models.Hospital(
        hospital_id=req.hospital_id,
        name=req.name,
        password_hash=auth.hash_password(req.password),
        latitude=req.latitude,
        longitude=req.longitude,
        contact=req.contact,
    )
    db.add(new_hosp)
    db.commit()
    return {"message": "Hospital registered successfully"}

@app.post("/api/auth/login", response_model=schemas.LoginResponse)
def login(req: schemas.LoginRequest, db: Session = Depends(get_db)):
    if req.role == "hospital":
        hosp = db.query(models.Hospital).filter(models.Hospital.hospital_id == req.identifier).first()
        if not hosp or not auth.verify_password(req.password or "", hosp.password_hash):
            raise HTTPException(status_code=401, detail="Invalid credentials")
        token = auth.create_access_token({"sub": req.identifier, "role": req.role})
        return {"token": token, "role": req.role, "identifier": req.identifier, "name": hosp.name}
    elif req.role == "ambulance":
        amb = db.query(models.Ambulance).filter(models.Ambulance.vehicle_number == req.identifier).first()
        if not amb:
            amb = models.Ambulance(vehicle_number=req.identifier)
            db.add(amb)
            db.commit()
        token = auth.create_access_token({"sub": req.identifier, "role": req.role})
        return {"token": token, "role": req.role, "identifier": req.identifier}
    elif req.role == "vehicle":
        veh = db.query(models.Vehicle).filter(models.Vehicle.vehicle_number == req.identifier).first()
        if not veh:
            veh = models.Vehicle(vehicle_number=req.identifier)
            db.add(veh)
            db.commit()
        token = auth.create_access_token({"sub": req.identifier, "role": req.role})
        return {"token": token, "role": req.role, "identifier": req.identifier}
    raise HTTPException(status_code=400, detail="Invalid role")

@app.get("/api/hospitals")
def get_hospitals(db: Session = Depends(get_db)):
    hospitals = db.query(models.Hospital).all()
    return [
        {"hospital_id": h.hospital_id, "name": h.name, "latitude": h.latitude, "longitude": h.longitude}
        for h in hospitals
    ]

@app.post("/api/emergency/activate")
async def activate_emergency(req: schemas.EmergencyActivate, db: Session = Depends(get_db)):
    amb = db.query(models.Ambulance).filter(models.Ambulance.vehicle_number == req.ambulance_id).first()
    if not amb:
        raise HTTPException(status_code=404, detail="Ambulance not found")
    amb.status = "emergency"
    amb.destination_lat = req.destination_lat
    amb.destination_lng = req.destination_lng
    emer = models.Emergency(ambulance_id=req.ambulance_id, hospital_id=req.hospital_id, severity=req.severity)
    db.add(emer)
    db.commit()
    # Also update sim_state
    sim_state["emergency_active"] = True
    sim_state["arrived"] = False
    sim_state["vehicles_alerted"] = 0
    update_computed_state()
    await broadcast({"type": "emergency_activated", "ambulance_id": req.ambulance_id, **sim_state, "vehicles": _vehicle_list()})
    return {"message": "Emergency activated"}

@app.post("/api/emergency/deactivate")
async def deactivate_emergency(req: schemas.EmergencyDeactivate, db: Session = Depends(get_db)):
    amb = db.query(models.Ambulance).filter(models.Ambulance.vehicle_number == req.ambulance_id).first()
    if amb:
        amb.status = "idle"
    emer = db.query(models.Emergency).filter(
        models.Emergency.ambulance_id == req.ambulance_id,
        models.Emergency.status == "active",
    ).first()
    if emer:
        emer.status = "ended"
        emer.end_time = datetime.utcnow()
    db.commit()
    sim_state["emergency_active"] = False
    sim_state["vehicle_instruction"] = "STANDBY"
    await broadcast({"type": "emergency_deactivated", "ambulance_id": req.ambulance_id, **sim_state, "vehicles": _vehicle_list()})
    return {"message": "Emergency deactivated"}

@app.post("/api/location/update")
async def update_location(req: schemas.LocationUpdate, db: Session = Depends(get_db)):
    amb = db.query(models.Ambulance).filter(models.Ambulance.vehicle_number == req.identifier).first()
    if amb:
        amb.latitude  = req.latitude
        amb.longitude = req.longitude
        amb.speed     = req.speed
        amb.last_seen = datetime.utcnow()
        # Sync to sim_state
        sim_state["ambulance_lat"]     = req.latitude
        sim_state["ambulance_lng"]     = req.longitude
        sim_state["ambulance_speed"]   = req.speed or 40
        sim_state["ambulance_heading"] = req.heading or 0
    else:
        veh = db.query(models.Vehicle).filter(models.Vehicle.vehicle_number == req.identifier).first()
        if veh:
            veh.latitude  = req.latitude
            veh.longitude = req.longitude
            veh.speed     = req.speed
            veh.last_seen = datetime.utcnow()
            sim_state["vehicle_lat"] = req.latitude
            sim_state["vehicle_lng"] = req.longitude
    db.commit()
    update_computed_state()
    await broadcast({"type": "state_update", **sim_state, "vehicles": _vehicle_list()})
    return {"message": "Location updated"}

@app.post("/api/alert/{alert_id}/acknowledge")
async def acknowledge_alert(alert_id: int, req: schemas.AlertAck, db: Session = Depends(get_db)):
    alert = db.query(models.Alert).filter(models.Alert.id == alert_id).first()
    if alert:
        alert.acknowledged = True
        db.commit()
        await broadcast({"type": "alert_acknowledged", "alert_id": alert_id, "vehicle_number": alert.vehicle_number})
    return {"message": "Alert acknowledged"}

@app.get("/api/emergency/active")
def get_active_emergencies(db: Session = Depends(get_db)):
    emergencies = db.query(models.Emergency).filter(models.Emergency.status == "active").all()
    result = []
    for e in emergencies:
        amb = db.query(models.Ambulance).filter(models.Ambulance.vehicle_number == e.ambulance_id).first()
        result.append({
            "id":            e.id,
            "ambulance_id":  e.ambulance_id,
            "hospital_id":   e.hospital_id,
            "severity":      e.severity,
            "start_time":    e.start_time,
            "ambulance_lat": amb.latitude  if amb else None,
            "ambulance_lng": amb.longitude if amb else None,
        })
    return result

@app.get("/api/emergency/history")
def get_emergency_history(db: Session = Depends(get_db)):
    return db.query(models.Emergency).filter(models.Emergency.status == "ended").all()

@app.get("/api/system/status", response_model=schemas.SystemStatus)
def get_system_status(db: Session = Depends(get_db)):
    try:
        db.execute(text("SELECT 1"))
        db_status = {"status": "ok", "details": "Connected"}
    except Exception as e:
        db_status = {"status": "error", "details": str(e)}
    num_clients = sum(len(c) for c in connections.values())
    return {
        "database":  db_status,
        "websocket": {"status": "ok", "details": f"{num_clients} connected clients"},
        "api":       {"status": "ok", "details": "Running"},
    }

@app.post("/api/esp32/update")
async def update_esp32(req: schemas.ESP32Update, db: Session = Depends(get_db)):
    await update_location(
        schemas.LocationUpdate(
            identifier=req.device_id,
            latitude=req.latitude,
            longitude=req.longitude,
            speed=req.speed,
        ),
        db,
    )
    if req.buzzer_status:
        amb = db.query(models.Ambulance).filter(models.Ambulance.vehicle_number == req.device_id).first()
        if amb and amb.status != "emergency":
            await activate_emergency(
                schemas.EmergencyActivate(ambulance_id=req.device_id, severity="critical"), db
            )
    return {"message": "ESP32 updated"}

# ─────────────────────────────────────────────────────────────
# Serve frontend static files at root
# ─────────────────────────────────────────────────────────────
frontend_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "frontend")
if not os.path.exists(frontend_dir):
    os.makedirs(frontend_dir)
app.mount("/", StaticFiles(directory=frontend_dir, html=True), name="static")
