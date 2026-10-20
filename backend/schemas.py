from pydantic import BaseModel
from typing import Optional

class HospitalRegister(BaseModel):
    hospital_id: str
    name: str
    password: str
    latitude: Optional[float] = 19.0760
    longitude: Optional[float] = 72.8777
    contact: Optional[str] = None

class LoginRequest(BaseModel):
    identifier: str
    password: Optional[str] = None
    role: str # 'hospital', 'ambulance', 'vehicle'

class LoginResponse(BaseModel):
    token: str
    role: str
    identifier: str
    name: Optional[str] = None

class LocationUpdate(BaseModel):
    identifier: str
    latitude: float
    longitude: float
    speed: Optional[float] = 0.0
    heading: Optional[float] = 0.0

class EmergencyActivate(BaseModel):
    ambulance_id: str
    hospital_id: Optional[str] = None
    severity: Optional[str] = 'critical'
    destination_lat: Optional[float] = None
    destination_lng: Optional[float] = None

class EmergencyDeactivate(BaseModel):
    ambulance_id: str

class AlertAck(BaseModel):
    alert_id: int

class ESP32Update(BaseModel):
    device_id: str
    latitude: float
    longitude: float
    speed: Optional[float] = 0.0
    heading: Optional[float] = 0.0
    emergency_active: Optional[bool] = False
    buzzer_status: Optional[bool] = False

class ServiceStatus(BaseModel):
    status: str
    details: Optional[str] = None

class SystemStatus(BaseModel):
    database: ServiceStatus
    websocket: ServiceStatus
    api: ServiceStatus
